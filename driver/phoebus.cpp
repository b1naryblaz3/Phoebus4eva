// SPDX-License-Identifier: MIT
// Experimental hardware-backed stereo PCM reconstruction. See docs/driver-status.md.
// This is original code using public WDK contracts and recovered protocol facts.
#include <ntddk.h>
#include <initguid.h>
#include <portcls.h>
#include <hdaudio.h>
#include "include/ph_hda_spec.h"
#include "include/ph_position.h"
#include "include/ph_dma_state.h"

void* __cdecl operator new(size_t, void* p) noexcept { return p; }
void __cdecl operator delete(void*, void*) noexcept {}

static constexpr ULONG PoolTag = 'WbhP';
static void* Allocate(SIZE_T n) { return ExAllocatePool2(POOL_FLAG_NON_PAGED, n, PoolTag); }
static void Free(void* p) { if (p) ExFreePoolWithTag(p, PoolTag); }
void __cdecl operator delete(void* p) noexcept { Free(p); }
void __cdecl operator delete(void* p, size_t) noexcept { Free(p); }
static bool Equal(REFIID a, REFIID b) { return IsEqualGUIDAligned(a, b) != FALSE; }
static void Failure(const char* stage, NTSTATUS status) {
    DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL,
        "PhoebusW11: %s failed: %08lx\n", stage, static_cast<ULONG>(status));
}

struct Widget {
    ULONG caps, pcm, formats, config, pinCaps;
    bool present;
};
struct Route {
    UCHAR pin, converter, connection;
    bool digital, found;
    const GUID* category;
    PWSTR waveName;
    PWSTR topoName;
};
class PassiveGuard {
    KMUTEX* lock_;
public:
    explicit PassiveGuard(KMUTEX* lock) : lock_(lock) {
        KeWaitForSingleObject(lock_, Executive, KernelMode, FALSE, nullptr);
    }
    ~PassiveGuard() { KeReleaseMutex(lock_, FALSE); }
};
class Stream;
class Adapter final : public IAdapterPowerManagement {
    volatile LONG refs_ = 1;
    KMUTEX verbs_;
    bool ownsBus_ = false;
    volatile LONG removed_ = 0;
    volatile LONG powered_ = 1;
    KSPIN_LOCK registerLock_;
    HDAUDIO_CODEC_TRANSFER transfer_{};
public:
    KMUTEX operations;
    LIST_ENTRY streams;
    HDAUDIO_BUS_INTERFACE_V2 bus{};
    UCHAR codec = 0, group = 0;
    Widget widgets[128]{};
    volatile LONG owners[128]{};
    Route routes[3]{};
    Adapter() {
        KeInitializeMutex(&verbs_, 0); KeInitializeMutex(&operations, 0);
        InitializeListHead(&streams); KeInitializeSpinLock(&registerLock_);
    }
    ~Adapter() {
        if (ownsBus_ && bus.InterfaceDereference) bus.InterfaceDereference(bus.Context);
    }
    STDMETHODIMP_(NTSTATUS) QueryInterface(REFIID id, void** out) override {
        if (!out) return STATUS_INVALID_PARAMETER;
        *out = nullptr;
        if (Equal(id, IID_IUnknown) || Equal(id, IID_IAdapterPowerManagement))
            *out = static_cast<IAdapterPowerManagement*>(this);
        if (!*out) return STATUS_NOINTERFACE;
        AddRef(); return STATUS_SUCCESS;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG n = InterlockedDecrement(&refs_);
        if (!n) { this->~Adapter(); Free(this); }
        return static_cast<ULONG>(n);
    }
    __declspec(code_seg("PAGE")) STDMETHODIMP_(void) PowerChangeState(POWER_STATE state) override {
        PAGED_CODE();
        PassiveGuard operation(&operations);
        PassiveGuard verb(&verbs_);
        KIRQL old;
        KeAcquireSpinLock(&registerLock_, &old);
        InterlockedExchange(&powered_, state.DeviceState == PowerDeviceD0 ? 1 : 0);
        KeReleaseSpinLock(&registerLock_, old);
    }
    STDMETHODIMP_(NTSTATUS) QueryPowerChangeState(POWER_STATE) override { return STATUS_SUCCESS; }
    STDMETHODIMP_(NTSTATUS) QueryDeviceCapabilities(PDEVICE_CAPABILITIES) override { return STATUS_SUCCESS; }
    bool Online() const { return !removed_ && powered_; }
    void Remove();
    bool ReadPosition(PULONG reg, ULONG& value) {
        KIRQL old;
        KeAcquireSpinLock(&registerLock_, &old);
        bool readable = reg && Online();
        value = readable ? READ_REGISTER_ULONG(reg) : 0;
        KeReleaseSpinLock(&registerLock_, old);
        return readable;
    }
    NTSTATUS Verb(UCHAR node, ULONG verb, ULONG payload, PULONG answer = nullptr) {
        if (KeGetCurrentIrql() != PASSIVE_LEVEL || !Online()) return STATUS_INVALID_DEVICE_STATE;
        uint32_t command = 0;
        int valid = verb <= 15 ? PhHdaEncodeVerb16(codec, node, verb, payload, &command) :
            PhHdaEncodeVerb8(codec, node, verb, payload, &command);
        if (!valid) return STATUS_INVALID_PARAMETER;
        PassiveGuard verbLock(&verbs_);
        if (!Online()) return STATUS_DEVICE_NOT_READY;
        RtlZeroMemory(&transfer_, sizeof(transfer_));
        transfer_.Output.Command = command;
        NTSTATUS s = bus.TransferCodecVerbs(bus.Context, 1, &transfer_, nullptr, nullptr);
        if (NT_SUCCESS(s) && (!transfer_.Input.IsValid || transfer_.Input.HasFifoOverrun ||
            transfer_.Input.IsUnsolicitedResponse)) s = STATUS_IO_DEVICE_ERROR;
        if (answer) *answer = NT_SUCCESS(s) ? transfer_.Input.Response : 0;
        return s;
    }
    NTSTATUS Parameter(UCHAR node, ULONG parameter, PULONG answer) {
        return Verb(node, PhHdaGetParameter, parameter, answer);
    }
    NTSTATUS Attach(PDEVICE_OBJECT fdo) {
        PIRP irp = IoAllocateIrp(fdo->StackSize, FALSE);
        if (!irp) return STATUS_INSUFFICIENT_RESOURCES;
        irp->RequestorMode = KernelMode;
        irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        PIO_STACK_LOCATION stack = IoGetNextIrpStackLocation(irp);
        stack->MajorFunction = IRP_MJ_PNP;
        stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
        stack->Parameters.QueryInterface.InterfaceType = &GUID_HDAUDIO_BUS_INTERFACE_V2;
        stack->Parameters.QueryInterface.Size = sizeof(bus);
        stack->Parameters.QueryInterface.Version = 0x100;
        stack->Parameters.QueryInterface.Interface = reinterpret_cast<PINTERFACE>(&bus);
        stack->Parameters.QueryInterface.InterfaceSpecificData = nullptr;
        NTSTATUS s = PcForwardIrpSynchronous(fdo, irp);
        IoFreeIrp(irp);
        if (!NT_SUCCESS(s)) return s;
        ownsBus_ = true; // Successful QUERY_INTERFACE supplies an owned reference.
        if (bus.Size < sizeof(bus) || bus.Version != 0x100 || !bus.Context ||
            !bus.InterfaceDereference || !bus.TransferCodecVerbs ||
            !bus.AllocateRenderDmaEngine || !bus.AllocateDmaBuffer || !bus.FreeDmaBuffer ||
            !bus.FreeDmaEngine || !bus.SetDmaEngineState || !bus.ChangeBandwidthAllocation ||
            !bus.GetLinkPositionRegister || !bus.GetResourceInformation ||
            !bus.AllocateDmaBufferWithNotification || !bus.FreeDmaBufferWithNotification ||
            !bus.RegisterNotificationEvent || !bus.UnregisterNotificationEvent)
            return STATUS_NOT_SUPPORTED;
        bus.GetResourceInformation(bus.Context, &codec, &group);
        if (codec > 14 || !group || group > 127) return STATUS_NOT_SUPPORTED;
        ULONG id = 0, subsystem = 0, type = 0;
        s = Parameter(0, PhHdaVendorId, &id);
        if (NT_SUCCESS(s)) s = Verb(group, 0xf20, 0, &subsystem);
        if (!NT_SUCCESS(s)) return s;
        if (id != 0x13f68888 || subsystem != 0x104384d5) return STATUS_DEVICE_CONFIGURATION_ERROR;
        s = Parameter(group, PhHdaFunctionGroupType, &type);
        if (!NT_SUCCESS(s) || (type & 255) != 1) return STATUS_NOT_SUPPORTED;
        // Recovered startup uses function-group D0/reset. Re-read all capabilities.
        s = Verb(group, PhHdaSetPowerState, 0);
        if (NT_SUCCESS(s)) s = Verb(group, PhHdaFunctionReset, 0);
        if (!NT_SUCCESS(s)) return s;
        KeStallExecutionProcessor(100);
        s = Verb(group, PhHdaSetPowerState, 0);
        if (!NT_SUCCESS(s)) return s;
        return Discover();
    }
    NTSTATUS Discover() {
        ULONG nodes = 0, groupPcm = 0, groupFormats = 0;
        NTSTATUS s = Parameter(group, PhHdaSubordinateNodeCount, &nodes);
        if (NT_SUCCESS(s)) s = Parameter(group, PhHdaPcmSizeRates, &groupPcm);
        if (NT_SUCCESS(s)) s = Parameter(group, PhHdaStreamFormats, &groupFormats);
        if (!NT_SUCCESS(s)) return s;
        ULONG start = (nodes >> 16) & 255, count = nodes & 255;
        if (!start || !count || start + count > RTL_NUMBER_OF(widgets)) return STATUS_NOT_SUPPORTED;
        routes[0].category = &KSNODETYPE_SPEAKER;
        routes[0].waveName = const_cast<PWSTR>(L"eLineOutWave");
        routes[0].topoName = const_cast<PWSTR>(L"eLineOutTopo");
        routes[1].category = &KSNODETYPE_HEADPHONES;
        routes[1].waveName = const_cast<PWSTR>(L"eHPOutWave");
        routes[1].topoName = const_cast<PWSTR>(L"eHPOutTopo");
        routes[2].category = &KSNODETYPE_SPDIF_INTERFACE;
        routes[2].waveName = const_cast<PWSTR>(L"eSpdifOutWave");
        routes[2].topoName = const_cast<PWSTR>(L"eSpdifOutTopo");
        routes[2].digital = true;
        for (ULONG n = start; n < start + count; ++n) {
            Widget& w = widgets[n];
            s = Parameter(static_cast<UCHAR>(n), PhHdaAudioWidgetCapabilities, &w.caps);
            if (!NT_SUCCESS(s)) return s;
            w.present = true;
            if (((w.caps >> 20) & 15) == 0) {
                w.pcm = groupPcm; w.formats = groupFormats;
                if (w.caps & (1u << 4)) {
                    s = Parameter(static_cast<UCHAR>(n), PhHdaPcmSizeRates, &w.pcm);
                    if (NT_SUCCESS(s)) s = Parameter(static_cast<UCHAR>(n), PhHdaStreamFormats, &w.formats);
                    if (!NT_SUCCESS(s)) return s;
                }
            } else if (((w.caps >> 20) & 15) == 4) {
                s = Verb(static_cast<UCHAR>(n), PhHdaGetConfigurationDefault, 0, &w.config);
                if (!NT_SUCCESS(s)) return s;
            }
        }
        ULONG found = 0;
        for (ULONG n = start; n < start + count; ++n) {
            Widget& w = widgets[n];
            if (((w.caps >> 20) & 15) != 4 || (w.config >> 30) == 1) continue;
            ULONG kind = (w.config >> 20) & 15;
            ULONG slot = kind == 0 ? 0 : kind == 2 ? 1 : kind == 4 ? 2 : 3;
            if (slot == 3 || routes[slot].found) continue;
            ULONG pinCaps = 0, length = 0;
            s = Parameter(static_cast<UCHAR>(n), PhHdaPinCapabilities, &pinCaps);
            if (!NT_SUCCESS(s)) return s;
            w.pinCaps = pinCaps;
            if (!(pinCaps & (1u << 4))) continue;
            s = Parameter(static_cast<UCHAR>(n), PhHdaConnectionListLength, &length);
            if (!NT_SUCCESS(s)) return s;
            // Baseline accepts direct short-form, non-range pin-to-DAC routes.
            // It fails closed for mixer/selector/range graphs still being recovered.
            ULONG entries = length & 127;
            if ((length & 128) || !entries || entries > 16) continue;
            for (ULONG i = 0; i < entries; ++i) {
                ULONG packed = 0;
                s = Verb(static_cast<UCHAR>(n), PhHdaGetConnectionListEntry, i & ~3u, &packed);
                if (!NT_SUCCESS(s)) return s;
                ULONG child = (packed >> ((i & 3) * 8)) & 255;
                if ((child & 128) || !child || child >= RTL_NUMBER_OF(widgets)) continue;
                Widget& dac = widgets[child];
                PhHdaPcmFormat baseline{48000, 16, 16, 2};
                if (!dac.present || !PhHdaConverterSupportsPcm(dac.caps, dac.pcm, dac.formats,
                    &baseline, routes[slot].digital)) continue;
                if (!routes[slot].digital && (dac.caps & (1u << 9))) continue;
                Route& r = routes[slot];
                r.pin = static_cast<UCHAR>(n); r.converter = static_cast<UCHAR>(child);
                r.connection = static_cast<UCHAR>(i); r.found = true; ++found; break;
            }
        }
        return found ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
    }
    bool Supports(const Route& route, const PhHdaPcmFormat& f) const {
        const Widget& w = widgets[route.converter];
        return f.channels == 2 && (f.valid_bits == 16 || f.valid_bits == 24 || f.valid_bits == 32) &&
            PhHdaConverterSupportsPcm(w.caps, w.pcm, w.formats, &f, route.digital);
    }
    NTSTATUS Unmute(UCHAR node) {
        const Widget& w = widgets[node];
        if (!(w.caps & (1u << 2))) return STATUS_SUCCESS;
        ULONG cap = 0;
        NTSTATUS s = Parameter((w.caps & (1u << 3)) ? node : group, 0x12, &cap);
        if (!NT_SUCCESS(s)) return s;
        // Set output L/R gain to codec-declared 0 dB offset, clear mute.
        return Verb(node, 3, 0xb000u | (cap & 127));
    }
    NTSTATUS Configure(const Route& r, USHORT format, UCHAR streamId) {
        if (!streamId || streamId > 15) return STATUS_INVALID_PARAMETER;
        NTSTATUS s = Verb(group, PhHdaSetPowerState, 0);
        if (NT_SUCCESS(s) && (widgets[r.converter].caps & (1u << 10)))
            s = Verb(r.converter, PhHdaSetPowerState, 0);
        if (NT_SUCCESS(s) && (widgets[r.pin].caps & (1u << 10)))
            s = Verb(r.pin, PhHdaSetPowerState, 0);
        if (NT_SUCCESS(s)) s = Verb(r.pin, PhHdaSetConnectionSelect, r.connection);
        if (NT_SUCCESS(s)) s = Verb(r.converter, 2, format);
        if (NT_SUCCESS(s)) s = Verb(r.converter, PhHdaSetConverterStreamChannel, streamId << 4);
        ULONG control = 0;
        if (NT_SUCCESS(s)) s = Verb(r.pin, PhHdaGetPinWidgetControl, 0, &control);
        if (NT_SUCCESS(s)) s = Verb(r.pin, PhHdaSetPinWidgetControl,
            (control & 0x1fu) | 0x40u | (r.category == &KSNODETYPE_HEADPHONES &&
            (widgets[r.pin].pinCaps & 8u) ? 0x80u : 0));
        if (NT_SUCCESS(s)) s = Unmute(r.converter);
        if (NT_SUCCESS(s)) s = Unmute(r.pin);
        if (NT_SUCCESS(s) && r.digital) {
            s = Verb(r.converter, PhHdaGetDigitalConverterControl, 0, &control);
            if (NT_SUCCESS(s)) s = Verb(r.converter, PhHdaSetDigitalConverterControl1,
                ((control & 255u) & ~0x20u) | 1u); // PCM, digital enable.
        }
        return s;
    }
};

static NTSTATUS ParseFormat(Adapter* adapter, const Route& route,
    PKSDATAFORMAT data, PhHdaPcmFormat& out) {
    if (!data || data->FormatSize < sizeof(KSDATAFORMAT) + sizeof(PCMWAVEFORMAT) ||
        !Equal(data->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
        !Equal(data->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
        !Equal(data->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)) return STATUS_INVALID_PARAMETER;
    auto* w = reinterpret_cast<PWAVEFORMATEX>(data + 1);
    out = { w->nSamplesPerSec, w->wBitsPerSample, w->wBitsPerSample, w->nChannels };
    if (w->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        if (data->FormatSize < sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE) || w->cbSize < 22)
            return STATUS_INVALID_PARAMETER;
        auto* ext = reinterpret_cast<PWAVEFORMATEXTENSIBLE>(w);
        if (!Equal(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) || ext->dwChannelMask != 3)
            return STATUS_INVALID_PARAMETER;
        out.valid_bits = ext->Samples.wValidBitsPerSample;
    } else if (w->wFormatTag != WAVE_FORMAT_PCM || w->wBitsPerSample != 16)
        return STATUS_NOT_SUPPORTED;
    ULONG alignment = (out.channels * out.container_bits) / 8;
    if (!adapter->Supports(route, out) || w->nBlockAlign != alignment ||
        w->nAvgBytesPerSec != out.sample_rate * alignment) return STATUS_NOT_SUPPORTED;
    return STATUS_SUCCESS;
}

class Stream final : public IMiniportWaveRTStreamNotification {
    volatile LONG refs_ = 1;
    Adapter* adapter_;
    Route route_;
    HANDLE engine_ = nullptr;
    PMDL mdl_ = nullptr;
    ULONG bytes_ = 0, fifo_ = 0, offset_ = 0;
    SIZE_T ownedBytes_ = 0;
    UCHAR streamId_ = 0;
    PULONG position_ = nullptr;
    bool notification_ = false;
    phoebus::DmaStatePlanner hardware_;
    PhHdaPcmFormat format_{};
    USHORT converterFormat_ = 0;
    KSPIN_LOCK cursorLock_;
    phoebus::PositionTracker cursor_;
    PKEVENT events_[8]{};
    bool reserved_ = false;
    bool listed_ = false;
    bool engineAllocated_ = false;
public:
    LIST_ENTRY link{};
    bool retained = false; // Registry owns a reference only after failed final cleanup.
    Stream(Adapter* a, const Route& r) : adapter_(a), route_(r) {
        a->AddRef(); KeInitializeSpinLock(&cursorLock_);
    }
    ~Stream() {
        {
            PassiveGuard operation(&adapter_->operations);
            if (listed_) RemoveEntryList(&link);
            if (reserved_) InterlockedExchange(&adapter_->owners[route_.converter], 0);
        }
        adapter_->Release();
    }
    STDMETHODIMP_(NTSTATUS) QueryInterface(REFIID id, void** out) override {
        if (!out) return STATUS_INVALID_PARAMETER;
        *out = nullptr;
        if (Equal(id, IID_IUnknown) || Equal(id, IID_IMiniportWaveRTStream))
            *out = static_cast<IMiniportWaveRTStream*>(this);
        else if (Equal(id, IID_IMiniportWaveRTStreamNotification))
            *out = static_cast<IMiniportWaveRTStreamNotification*>(this);
        if (!*out) return STATUS_NOINTERFACE;
        AddRef(); return STATUS_SUCCESS;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG n = InterlockedDecrement(&refs_);
        if (!n) {
            NTSTATUS s = Cleanup();
            if (!NT_SUCCESS(s)) {
                Failure("stream teardown (ownership retained)", s);
                // The adapter registry is the concrete owner; removal retries it.
                PassiveGuard operation(&adapter_->operations);
                retained = true;
                InterlockedExchange(&refs_, 1); return 1;
            }
            this->~Stream(); Free(this);
        }
        return static_cast<ULONG>(n);
    }
    NTSTATUS Initialize(PKSDATAFORMAT data) {
        PassiveGuard operation(&adapter_->operations);
        if (!adapter_->Online()) return STATUS_DEVICE_NOT_READY;
        if (InterlockedCompareExchange(&adapter_->owners[route_.converter], 1, 0))
            return STATUS_DEVICE_BUSY;
        reserved_ = true;
        InsertTailList(&adapter_->streams, &link); listed_ = true;
        NTSTATUS s = ParseFormat(adapter_, route_, data, format_);
        if (!NT_SUCCESS(s)) return s;
        HDAUDIO_STREAM_FORMAT h{ format_.sample_rate, format_.valid_bits,
            format_.container_bits, format_.channels };
        HDAUDIO_CONVERTER_FORMAT converter{};
        s = adapter_->bus.AllocateRenderDmaEngine(adapter_->bus.Context, &h, FALSE, &engine_, &converter);
        if (NT_SUCCESS(s)) { converterFormat_ = converter.ConverterFormat; engineAllocated_ = true; }
        return s;
    }
    STDMETHODIMP_(NTSTATUS) SetFormat(PKSDATAFORMAT data) override {
        PassiveGuard operation(&adapter_->operations);
        if (!adapter_->Online() || !engineAllocated_) return STATUS_DEVICE_NOT_READY;
        if (mdl_ || hardware_.state() != phoebus::DmaState::Reset) return STATUS_INVALID_DEVICE_STATE;
        PhHdaPcmFormat f{};
        NTSTATUS s = ParseFormat(adapter_, route_, data, f);
        if (!NT_SUCCESS(s)) return s;
        HDAUDIO_STREAM_FORMAT h{ f.sample_rate, f.valid_bits, f.container_bits, f.channels };
        HDAUDIO_CONVERTER_FORMAT converter{};
        s = adapter_->bus.ChangeBandwidthAllocation(adapter_->bus.Context, engine_, &h, &converter);
        if (NT_SUCCESS(s)) { format_ = f; converterFormat_ = converter.ConverterFormat; }
        return s;
    }
    NTSTATUS HardwareState(HDAUDIO_STREAM_STATE next) {
        if (!engineAllocated_) return STATUS_INVALID_DEVICE_STATE;
        return static_cast<NTSTATUS>(hardware_.transition(static_cast<phoebus::DmaState>(next),
            mdl_ != nullptr, [this](phoebus::DmaState state) {
                return adapter_->bus.SetDmaEngineState(adapter_->bus.Context,
                    static_cast<HDAUDIO_STREAM_STATE>(state), 1, &engine_);
            }));
    }
    STDMETHODIMP_(NTSTATUS) SetState(KSSTATE state) override {
        PassiveGuard operation(&adapter_->operations);
        if (state < KSSTATE_STOP || state > KSSTATE_RUN) return STATUS_INVALID_PARAMETER;
        if (!adapter_->Online() || !engineAllocated_) {
            KIRQL old;
            KeAcquireSpinLock(&cursorLock_, &old);
            auto current = static_cast<KSSTATE>(cursor_.state);
            bool downward = state <= current;
            if (downward) cursor_.transition(static_cast<phoebus::StreamState>(state), 0, bytes_);
            KeReleaseSpinLock(&cursorLock_, old);
            return downward ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
        }
        if (state != KSSTATE_STOP && (!mdl_ || !adapter_->Online())) return STATUS_DEVICE_NOT_READY;
        if (state == KSSTATE_RUN) {
            NTSTATUS s = adapter_->Configure(route_, converterFormat_, streamId_);
            if (!NT_SUCCESS(s)) return s;
        }
        auto next = state == KSSTATE_STOP ? ResetState : state == KSSTATE_RUN ? RunState : PauseState;
        NTSTATUS s = HardwareState(next);
        if (!NT_SUCCESS(s)) return s;
        KIRQL old;
        KeAcquireSpinLock(&cursorLock_, &old);
        ULONG dma = 0;
        if (position_ && bytes_) adapter_->ReadPosition(position_, dma);
        cursor_.transition(static_cast<phoebus::StreamState>(state), dma, bytes_);
        KeReleaseSpinLock(&cursorLock_, old);
        return STATUS_SUCCESS;
    }
    STDMETHODIMP_(NTSTATUS) GetPosition(PKSAUDIO_POSITION position) override {
        if (!position) return STATUS_INVALID_PARAMETER;
        KIRQL old;
        KeAcquireSpinLock(&cursorLock_, &old);
        uint64_t play = 0, write = 0;
        ULONG dma = 0;
        bool ok = adapter_->ReadPosition(position_, dma) && cursor_.snapshot(dma, bytes_, fifo_, play, write);
        position->PlayOffset = play; position->WriteOffset = write;
        KeReleaseSpinLock(&cursorLock_, old);
        return ok ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
    }
    NTSTATUS Buffer(ULONG count, ULONG requested, PMDL* mdl, PULONG actual,
        PULONG offset, MEMORY_CACHING_TYPE* cache) {
        if (!mdl || !actual || !offset || !cache) return STATUS_INVALID_PARAMETER;
        *mdl = nullptr; *actual = *offset = 0; *cache = MmWriteCombined;
        ULONG frame = format_.channels * format_.container_bits / 8;
        if (!engine_ || mdl_ || hardware_.state() != phoebus::DmaState::Reset || !frame ||
            !requested || requested > 4u * 1024u * 1024u || requested % frame ||
            count > 2 || (count == 2 && requested % (frame * 2))) return STATUS_INVALID_PARAMETER;
        SIZE_T allocated = 0, pageOffset = 0;
        NTSTATUS s;
        if (count) s = adapter_->bus.AllocateDmaBufferWithNotification(adapter_->bus.Context,
            engine_, count, requested, &mdl_, &allocated, &pageOffset, &streamId_, &fifo_);
        else s = adapter_->bus.AllocateDmaBuffer(adapter_->bus.Context, engine_, requested,
            &mdl_, &allocated, &streamId_, &fifo_);
        if (!NT_SUCCESS(s)) return s;
        notification_ = count != 0;
        ownedBytes_ = allocated;
        // Save exact bus ownership before validating its narrowed WaveRT view.
        if (allocated > MAXULONG || pageOffset > MAXULONG || !allocated || allocated % frame ||
            (count == 2 && allocated % (frame * 2))) {
            s = count ? adapter_->bus.FreeDmaBufferWithNotification(adapter_->bus.Context,
                engine_, mdl_, allocated) : adapter_->bus.FreeDmaBuffer(adapter_->bus.Context, engine_);
            if (NT_SUCCESS(s)) { mdl_ = nullptr; ownedBytes_ = 0; }
            else Failure("reject malformed DMA allocation", s);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
        bytes_ = static_cast<ULONG>(allocated); offset_ = static_cast<ULONG>(pageOffset);
        s = adapter_->bus.GetLinkPositionRegister(adapter_->bus.Context, engine_, &position_);
        if (NT_SUCCESS(s)) s = adapter_->Configure(route_, converterFormat_, streamId_);
        if (!NT_SUCCESS(s)) { NTSTATUS cleanup = FreeBuffer(); if (!NT_SUCCESS(cleanup)) Failure("buffer unwind", cleanup); return s; }
        *mdl = mdl_; *actual = bytes_; *offset = offset_;
        return STATUS_SUCCESS;
    }
    STDMETHODIMP_(NTSTATUS) AllocateAudioBuffer(ULONG requested, PMDL* mdl, PULONG actual,
        PULONG offset, MEMORY_CACHING_TYPE* cache) override {
        PassiveGuard operation(&adapter_->operations);
        if (!adapter_->Online() || !engineAllocated_) return STATUS_DEVICE_NOT_READY;
        return Buffer(0, requested, mdl, actual, offset, cache);
    }
    STDMETHODIMP_(NTSTATUS) AllocateBufferWithNotification(ULONG count, ULONG requested,
        PMDL* mdl, PULONG actual, PULONG offset, MEMORY_CACHING_TYPE* cache) override {
        PassiveGuard operation(&adapter_->operations);
        if (!adapter_->Online() || !engineAllocated_) return STATUS_DEVICE_NOT_READY;
        if (count != 1 && count != 2) return STATUS_INVALID_PARAMETER;
        return Buffer(count, requested, mdl, actual, offset, cache);
    }
    NTSTATUS FreeBuffer() {
        if (!mdl_) return STATUS_SUCCESS;
        NTSTATUS s = engineAllocated_ ? HardwareState(ResetState) : STATUS_SUCCESS;
        NTSTATUS stopStatus = s;
        NTSTATUS eventStatus = UnregisterAllEvents();
        if (!NT_SUCCESS(stopStatus)) return stopStatus;
        if (!NT_SUCCESS(eventStatus)) return eventStatus;
        if (adapter_->Online() && engineAllocated_) {
            s = adapter_->Verb(route_.converter, PhHdaSetConverterStreamChannel, 0);
            if (!NT_SUCCESS(s)) return s;
        }
        KIRQL old;
        KeAcquireSpinLock(&cursorLock_, &old);
        position_ = nullptr;
        cursor_.transition(phoebus::StreamState::Stop, 0, bytes_);
        KeReleaseSpinLock(&cursorLock_, old);
        s = notification_ ? adapter_->bus.FreeDmaBufferWithNotification(adapter_->bus.Context,
            engine_, mdl_, ownedBytes_) : adapter_->bus.FreeDmaBuffer(adapter_->bus.Context, engine_);
        if (NT_SUCCESS(s)) {
            mdl_ = nullptr; ownedBytes_ = 0; bytes_ = fifo_ = offset_ = 0; notification_ = false;
            if (!engineAllocated_) engine_ = nullptr;
        }
        return s;
    }
    NTSTATUS UnregisterAllEvents() {
        NTSTATUS firstError = STATUS_SUCCESS;
        for (auto& e : events_) if (e) {
            NTSTATUS s = adapter_->bus.UnregisterNotificationEvent(adapter_->bus.Context, engine_, e);
            if (NT_SUCCESS(s)) e = nullptr;
            else if (NT_SUCCESS(firstError)) firstError = s;
        }
        return firstError;
    }
    NTSTATUS Cleanup() {
        PassiveGuard operation(&adapter_->operations);
        if (!engine_) return STATUS_SUCCESS;
        NTSTATUS s = FreeBuffer();
        if (NT_SUCCESS(s) && engineAllocated_) s = HardwareState(ResetState);
        if (NT_SUCCESS(s) && engineAllocated_) s = adapter_->bus.FreeDmaEngine(adapter_->bus.Context, engine_);
        if (NT_SUCCESS(s)) { engine_ = nullptr; engineAllocated_ = false; }
        return s;
    }
    NTSTATUS DetachForRemoval() {
        // Caller holds adapter operations. Windows 11 SR ordering: stop/reset/
        // release engine now; retain the bus MDL/token until PortCls closes it.
        if (!engineAllocated_) return STATUS_SUCCESS;
        NTSTATUS s = HardwareState(ResetState);
        NTSTATUS stopStatus = s;
        NTSTATUS eventStatus = UnregisterAllEvents();
        if (!NT_SUCCESS(stopStatus)) return stopStatus;
        if (!NT_SUCCESS(eventStatus)) return eventStatus;
        KIRQL old;
        KeAcquireSpinLock(&cursorLock_, &old);
        position_ = nullptr;
        KeReleaseSpinLock(&cursorLock_, old);
        s = adapter_->bus.FreeDmaEngine(adapter_->bus.Context, engine_);
        if (NT_SUCCESS(s)) {
            engineAllocated_ = false;
            if (!mdl_) engine_ = nullptr;
        }
        return s;
    }
    STDMETHODIMP_(void) FreeAudioBuffer(PMDL mdl, ULONG bytes) override {
        PassiveGuard operation(&adapter_->operations);
        if (mdl != mdl_ || bytes != bytes_) { Failure("mismatched buffer free", STATUS_INVALID_PARAMETER); return; }
        NTSTATUS s = FreeBuffer(); if (!NT_SUCCESS(s)) Failure("FreeAudioBuffer", s);
    }
    STDMETHODIMP_(void) FreeBufferWithNotification(PMDL mdl, ULONG bytes) override { FreeAudioBuffer(mdl, bytes); }
    STDMETHODIMP_(void) GetHWLatency(KSRTAUDIO_HWLATENCY* latency) override {
        if (!latency) return;
        RtlZeroMemory(latency, sizeof(*latency));
        latency->FifoSize = fifo_;
        // Link and board codec latency have not been measured; see status document.
    }
    STDMETHODIMP_(NTSTATUS) GetPositionRegister(KSRTAUDIO_HWREGISTER*) override { return STATUS_NOT_SUPPORTED; }
    STDMETHODIMP_(NTSTATUS) GetClockRegister(KSRTAUDIO_HWREGISTER*) override { return STATUS_NOT_SUPPORTED; }
    STDMETHODIMP_(NTSTATUS) RegisterNotificationEvent(PKEVENT event) override {
        PassiveGuard operation(&adapter_->operations);
        if (!adapter_->Online() || !engineAllocated_) return STATUS_DEVICE_NOT_READY;
        if (!event || !mdl_ || !notification_) return STATUS_INVALID_DEVICE_STATE;
        for (auto e : events_) if (e == event) return STATUS_OBJECT_NAME_COLLISION;
        for (auto& e : events_) if (!e) {
            NTSTATUS s = adapter_->bus.RegisterNotificationEvent(adapter_->bus.Context, engine_, event);
            if (NT_SUCCESS(s)) e = event;
            return s;
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    STDMETHODIMP_(NTSTATUS) UnregisterNotificationEvent(PKEVENT event) override {
        PassiveGuard operation(&adapter_->operations);
        if (!event) return STATUS_INVALID_PARAMETER;
        for (auto& e : events_) if (e == event && event) {
            NTSTATUS s = adapter_->bus.UnregisterNotificationEvent(adapter_->bus.Context, engine_, event);
            if (NT_SUCCESS(s)) e = nullptr;
            return s;
        }
        // A successful detach already removed events while retaining the buffer.
        return !engineAllocated_ ? STATUS_SUCCESS : STATUS_NOT_FOUND;
    }
};

void Adapter::Remove() {
    PassiveGuard operation(&operations);
    {
        PassiveGuard verb(&verbs_);
        KIRQL old;
        KeAcquireSpinLock(&registerLock_, &old);
        InterlockedExchange(&removed_, 1);
        KeReleaseSpinLock(&registerLock_, old);
    }
    for (PLIST_ENTRY item = streams.Flink; item != &streams; ) {
        PLIST_ENTRY next = item->Flink;
        Stream* stream = CONTAINING_RECORD(item, Stream, link);
        NTSTATUS s = stream->DetachForRemoval();
        if (!NT_SUCCESS(s)) Failure("surprise-removal detach", s);
        if (stream->retained) stream->Release(); // Registry's retained owner.
        item = next;
    }
}

static KSDATARANGE bridgeRange = {
    sizeof(KSDATARANGE), 0, 0, 0, STATICGUIDOF(KSDATAFORMAT_TYPE_AUDIO),
    STATICGUIDOF(KSDATAFORMAT_SUBTYPE_ANALOG), STATICGUIDOF(KSDATAFORMAT_SPECIFIER_NONE)
};
static PKSDATARANGE bridgeRanges[] = { &bridgeRange };
static PCCONNECTION_DESCRIPTOR directConnection = { PCFILTER_NODE, 0, PCFILTER_NODE, 1 };

class Topology final : public IMiniportTopology {
    volatile LONG refs_ = 1;
    Adapter* adapter_;
    PCPIN_DESCRIPTOR pins_[2]{};
    PCFILTER_DESCRIPTOR descriptor_{};
public:
    Topology(Adapter* a, const Route& r) : adapter_(a) {
        a->AddRef();
        for (ULONG i = 0; i < 2; ++i) {
            auto& p = pins_[i].KsPinDescriptor;
            p.DataRangesCount = 1; p.DataRanges = bridgeRanges;
            p.DataFlow = i ? KSPIN_DATAFLOW_OUT : KSPIN_DATAFLOW_IN;
            p.Communication = KSPIN_COMMUNICATION_NONE;
            p.Category = i ? r.category : &KSCATEGORY_AUDIO;
        }
        descriptor_.PinSize = sizeof(PCPIN_DESCRIPTOR); descriptor_.PinCount = 2;
        descriptor_.Pins = pins_; descriptor_.ConnectionCount = 1;
        descriptor_.Connections = &directConnection;
    }
    ~Topology() { adapter_->Release(); }
    STDMETHODIMP_(NTSTATUS) QueryInterface(REFIID id, void** out) override {
        if (!out) return STATUS_INVALID_PARAMETER;
        *out = nullptr;
        if (Equal(id, IID_IUnknown) || Equal(id, IID_IMiniport) || Equal(id, IID_IMiniportTopology))
            *out = static_cast<IMiniportTopology*>(this);
        if (!*out) return STATUS_NOINTERFACE;
        AddRef(); return STATUS_SUCCESS;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG n = InterlockedDecrement(&refs_); if (!n) { this->~Topology(); Free(this); }
        return static_cast<ULONG>(n);
    }
    STDMETHODIMP_(NTSTATUS) GetDescription(PPCFILTER_DESCRIPTOR* out) override {
        if (!out) return STATUS_INVALID_PARAMETER; *out = &descriptor_; return STATUS_SUCCESS;
    }
    STDMETHODIMP_(NTSTATUS) DataRangeIntersection(ULONG, PKSDATARANGE, PKSDATARANGE,
        ULONG, PVOID, PULONG length) override {
        if (length) *length = 0; return STATUS_NO_MATCH;
    }
    STDMETHODIMP_(NTSTATUS) Init(PUNKNOWN, PRESOURCELIST, PPORTTOPOLOGY) override { return STATUS_SUCCESS; }
};

class Wave final : public IMiniportWaveRT {
    volatile LONG refs_ = 1;
    Adapter* adapter_;
    Route route_;
    KSDATARANGE_AUDIO ranges_[12]{};
    PKSDATARANGE rangePointers_[12]{};
    PCPIN_DESCRIPTOR pins_[2]{};
    PCFILTER_DESCRIPTOR descriptor_{};
public:
    Wave(Adapter* a, const Route& r) : adapter_(a), route_(r) {
        a->AddRef();
        const ULONG rates[] = { 48000, 44100, 96000, 88200, 192000, 176400 };
        ULONG count = 0;
        const USHORT precisions[] = { 16, 24 };
        for (ULONG rate : rates) for (USHORT bits : precisions) {
            PhHdaPcmFormat f{ rate, bits, PhHdaContainerBits(bits), 2 };
            if (!a->Supports(r, f)) continue;
            auto& x = ranges_[count];
            x.DataRange.FormatSize = sizeof(x);
            x.DataRange.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
            x.DataRange.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
            x.DataRange.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
            x.MaximumChannels = 2;
            x.MinimumBitsPerSample = x.MaximumBitsPerSample = f.container_bits;
            x.MinimumSampleFrequency = x.MaximumSampleFrequency = rate;
            rangePointers_[count] = &x.DataRange; ++count;
        }
        auto& host = pins_[0];
        host.MaxGlobalInstanceCount = host.MaxFilterInstanceCount = 1;
        host.KsPinDescriptor.DataRangesCount = count;
        host.KsPinDescriptor.DataRanges = rangePointers_;
        host.KsPinDescriptor.DataFlow = KSPIN_DATAFLOW_IN;
        host.KsPinDescriptor.Communication = KSPIN_COMMUNICATION_SINK;
        host.KsPinDescriptor.Category = &KSCATEGORY_AUDIO;
        auto& bridge = pins_[1].KsPinDescriptor;
        bridge.DataRangesCount = 1; bridge.DataRanges = bridgeRanges;
        bridge.DataFlow = KSPIN_DATAFLOW_OUT; bridge.Communication = KSPIN_COMMUNICATION_NONE;
        bridge.Category = &KSCATEGORY_AUDIO;
        descriptor_.PinSize = sizeof(PCPIN_DESCRIPTOR); descriptor_.PinCount = 2;
        descriptor_.Pins = pins_; descriptor_.ConnectionCount = 1;
        descriptor_.Connections = &directConnection;
    }
    ~Wave() { adapter_->Release(); }
    STDMETHODIMP_(NTSTATUS) QueryInterface(REFIID id, void** out) override {
        if (!out) return STATUS_INVALID_PARAMETER;
        *out = nullptr;
        if (Equal(id, IID_IUnknown) || Equal(id, IID_IMiniport) || Equal(id, IID_IMiniportWaveRT))
            *out = static_cast<IMiniportWaveRT*>(this);
        if (!*out) return STATUS_NOINTERFACE;
        AddRef(); return STATUS_SUCCESS;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return static_cast<ULONG>(InterlockedIncrement(&refs_)); }
    STDMETHODIMP_(ULONG) Release() override {
        LONG n = InterlockedDecrement(&refs_); if (!n) { this->~Wave(); Free(this); }
        return static_cast<ULONG>(n);
    }
    STDMETHODIMP_(NTSTATUS) GetDescription(PPCFILTER_DESCRIPTOR* out) override {
        if (!out) return STATUS_INVALID_PARAMETER; *out = &descriptor_; return STATUS_SUCCESS;
    }
    STDMETHODIMP_(NTSTATUS) DataRangeIntersection(ULONG pin, PKSDATARANGE client,
        PKSDATARANGE matching, ULONG size, PVOID output, PULONG required) override {
        if (!required) return STATUS_INVALID_PARAMETER;
        *required = 0;
        if (pin || !client || !matching || client->FormatSize < sizeof(KSDATARANGE_AUDIO) ||
            matching->FormatSize < sizeof(KSDATARANGE_AUDIO) ||
            !Equal(client->MajorFormat, KSDATAFORMAT_TYPE_AUDIO) ||
            !Equal(client->SubFormat, KSDATAFORMAT_SUBTYPE_PCM) ||
            !Equal(client->Specifier, KSDATAFORMAT_SPECIFIER_WAVEFORMATEX)) return STATUS_NO_MATCH;
        auto* c = reinterpret_cast<PKSDATARANGE_AUDIO>(client);
        auto* m = reinterpret_cast<PKSDATARANGE_AUDIO>(matching);
        ULONG rate = m->MinimumSampleFrequency, container = m->MinimumBitsPerSample;
        if (c->MaximumChannels < 2 || rate < c->MinimumSampleFrequency || rate > c->MaximumSampleFrequency ||
            container < c->MinimumBitsPerSample || container > c->MaximumBitsPerSample) return STATUS_NO_MATCH;
        PhHdaPcmFormat f{ rate, static_cast<USHORT>(container == 32 ? 24 : 16),
            static_cast<USHORT>(container), 2 };
        if (!adapter_->Supports(route_, f)) return STATUS_NO_MATCH;
        *required = sizeof(KSDATAFORMAT_WAVEFORMATEXTENSIBLE);
        if (!size) return STATUS_BUFFER_OVERFLOW;
        if (size < *required) return STATUS_BUFFER_TOO_SMALL;
        if (!output) return STATUS_INVALID_PARAMETER;
        auto* result = static_cast<PKSDATAFORMAT_WAVEFORMATEXTENSIBLE>(output);
        RtlZeroMemory(result, sizeof(*result));
        result->DataFormat.FormatSize = sizeof(*result);
        result->DataFormat.SampleSize = 2 * container / 8;
        result->DataFormat.MajorFormat = KSDATAFORMAT_TYPE_AUDIO;
        result->DataFormat.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        result->DataFormat.Specifier = KSDATAFORMAT_SPECIFIER_WAVEFORMATEX;
        auto& w = result->WaveFormatExt.Format;
        w.wFormatTag = WAVE_FORMAT_EXTENSIBLE; w.nChannels = 2;
        w.nSamplesPerSec = rate; w.nBlockAlign = static_cast<WORD>(2 * container / 8);
        w.nAvgBytesPerSec = rate * w.nBlockAlign;
        w.wBitsPerSample = static_cast<WORD>(container); w.cbSize = 22;
        result->WaveFormatExt.Samples.wValidBitsPerSample = f.valid_bits;
        result->WaveFormatExt.dwChannelMask = 3;
        result->WaveFormatExt.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;
        return STATUS_SUCCESS;
    }
    STDMETHODIMP_(NTSTATUS) Init(PUNKNOWN, PRESOURCELIST, PPORTWAVERT) override { return STATUS_SUCCESS; }
    STDMETHODIMP_(NTSTATUS) NewStream(PMINIPORTWAVERTSTREAM* out, PPORTWAVERTSTREAM,
        ULONG pin, BOOLEAN capture, PKSDATAFORMAT format) override {
        if (!out) return STATUS_INVALID_PARAMETER;
        *out = nullptr;
        if (pin || capture) return STATUS_INVALID_PARAMETER;
        void* memory = Allocate(sizeof(Stream));
        if (!memory) return STATUS_INSUFFICIENT_RESOURCES;
        Stream* stream = new(memory) Stream(adapter_, route_);
        NTSTATUS s = stream->Initialize(format);
        if (!NT_SUCCESS(s)) { stream->Release(); return s; }
        *out = static_cast<IMiniportWaveRTStream*>(stream); return STATUS_SUCCESS;
    }
    STDMETHODIMP_(NTSTATUS) GetDeviceDescription(PDEVICE_DESCRIPTION out) override {
        if (!out) return STATUS_INVALID_PARAMETER;
        RtlZeroMemory(out, sizeof(*out));
        out->Version = DEVICE_DESCRIPTION_VERSION; out->Master = TRUE;
        out->ScatterGather = TRUE; out->Dma32BitAddresses = TRUE;
        out->Dma64BitAddresses = TRUE; out->InterfaceType = PCIBus;
        out->MaximumLength = 4u * 1024u * 1024u;
        return STATUS_SUCCESS;
    }
};

struct DeviceExtension { UCHAR portClass[PORT_CLASS_DEVICE_EXTENSION_SIZE]; Adapter* adapter; };
static NTSTATUS RegisterEndpoint(PDEVICE_OBJECT fdo, PIRP irp, PRESOURCELIST resources,
    Adapter* adapter, const Route& route) {
    PPORT topologyPort = nullptr, wavePort = nullptr;
    void* tm = Allocate(sizeof(Topology)); void* wm = Allocate(sizeof(Wave));
    if (!tm || !wm) { Free(tm); Free(wm); return STATUS_INSUFFICIENT_RESOURCES; }
    Topology* topology = new(tm) Topology(adapter, route);
    Wave* wave = new(wm) Wave(adapter, route);
    NTSTATUS s = PcNewPort(&topologyPort, CLSID_PortTopology);
    if (NT_SUCCESS(s)) s = topologyPort->Init(fdo, irp, topology, adapter, resources);
    if (NT_SUCCESS(s)) s = PcRegisterSubdevice(fdo, route.topoName, topologyPort);
    if (NT_SUCCESS(s)) s = PcNewPort(&wavePort, CLSID_PortWaveRT);
    if (NT_SUCCESS(s)) s = wavePort->Init(fdo, irp, wave, adapter, resources);
    if (NT_SUCCESS(s)) s = PcRegisterSubdevice(fdo, route.waveName, wavePort);
    if (NT_SUCCESS(s)) s = PcRegisterPhysicalConnection(fdo, wavePort, 1, topologyPort, 0);
    // PortCls owns successfully registered subdevices, including partial-start unwind.
    if (wavePort) wavePort->Release();
    if (topologyPort) topologyPort->Release();
    wave->Release(); topology->Release();
    return s;
}
static NTSTATUS StartDevice(PDEVICE_OBJECT fdo, PIRP irp, PRESOURCELIST resources) {
    auto* ext = static_cast<DeviceExtension*>(fdo->DeviceExtension);
    if (ext->adapter) return STATUS_INVALID_DEVICE_STATE;
    void* memory = Allocate(sizeof(Adapter));
    if (!memory) return STATUS_INSUFFICIENT_RESOURCES;
    Adapter* adapter = new(memory) Adapter();
    ext->adapter = adapter;
    NTSTATUS s = adapter->Attach(fdo);
    if (NT_SUCCESS(s)) s = PcRegisterAdapterPowerManagement(adapter, fdo);
    if (NT_SUCCESS(s)) for (const Route& route : adapter->routes) if (route.found) {
        s = RegisterEndpoint(fdo, irp, resources, adapter, route);
        if (!NT_SUCCESS(s)) break;
    }
    if (!NT_SUCCESS(s)) Failure("StartDevice", s);
    return s;
}
static NTSTATUS AddDevice(PDRIVER_OBJECT driver, PDEVICE_OBJECT physical) {
    return PcAddAdapterDevice(driver, physical, StartDevice, 6, sizeof(DeviceExtension));
}
static NTSTATUS Pnp(PDEVICE_OBJECT device, PIRP irp) {
    auto* ext = static_cast<DeviceExtension*>(device->DeviceExtension);
    UCHAR minor = IoGetCurrentIrpStackLocation(irp)->MinorFunction;
    if ((minor == IRP_MN_REMOVE_DEVICE || minor == IRP_MN_SURPRISE_REMOVAL) && ext->adapter)
        ext->adapter->Remove();
    if (minor == IRP_MN_REMOVE_DEVICE && ext->adapter) {
        ext->adapter->Release(); ext->adapter = nullptr;
    }
    return PcDispatchIrp(device, irp);
}
extern "C" DRIVER_INITIALIZE DriverEntry;
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driver, PUNICODE_STRING path) {
    NTSTATUS s = PcInitializeAdapterDriver(driver, path, AddDevice);
    if (NT_SUCCESS(s)) driver->MajorFunction[IRP_MJ_PNP] = Pnp;
    return s;
}
