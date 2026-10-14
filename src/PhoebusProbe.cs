// Read-only Windows Core Audio probe. No stream is initialized or started.
// COM declarations follow the Windows SDK's mmdeviceapi.h and audioclient.h.
using System;
using System.Collections.Generic;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Web.Script.Serialization;
using Microsoft.Win32;

namespace PhoebusDiagnostics
{
    internal static class Program
    {
        [STAThread]
        private static int Main(string[] args)
        {
            string destination = null, filter = null;
            for (int i = 0; i < args.Length; i++)
            {
                if (args[i] == "--help") { Usage(); return 0; }
                if ((args[i] == "--json" || args[i] == "--filter") && i + 1 < args.Length && !args[i + 1].StartsWith("--"))
                {
                    string option = args[i];
                    if (option == "--json") destination = args[++i]; else filter = args[++i];
                }
                else { Usage(); return 1; }
            }
            Report report = new Report();
            report.CapturedAtUtc = DateTime.UtcNow.ToString("o");
            report.Limitation = "Driver-advertised formats only. No playback, receiver capability, Dolby licence, latency or audible glitch verification.";
            try
            {
                using (RegistryKey key = Registry.LocalMachine.OpenSubKey(@"SOFTWARE\Microsoft\Windows NT\CurrentVersion"))
                {
                    if (key != null)
                        report.WindowsBuild = Convert.ToString(key.GetValue("DisplayVersion")) + " / " +
                            Convert.ToString(key.GetValue("CurrentBuildNumber")) + "." + Convert.ToString(key.GetValue("UBR"));
                }
            }
            catch (Exception e) { report.Errors.Add("Windows build: " + e.Message); }
            IMMDeviceEnumerator enumerator = null;
            IMMDeviceCollection collection = null;
            try
            {
                enumerator = (IMMDeviceEnumerator)new MMDeviceEnumerator();
                Check(enumerator.EnumAudioEndpoints(0, 1, out collection)); // eRender, DEVICE_STATE_ACTIVE
                uint count;
                Check(collection.GetCount(out count));
                for (uint i = 0; i < count; i++)
                {
                    IMMDevice device = null;
                    try
                    {
                        Check(collection.Item(i, out device));
                        string name = Convert.ToString(Property(device, "a45c254e-df1c-4efd-8020-67d146a850e0", 14));
                        if (filter != null && name.IndexOf(filter, StringComparison.OrdinalIgnoreCase) < 0) continue;
                        Endpoint endpoint = Inspect(device, name);
                        report.Endpoints.Add(endpoint);
                        Console.WriteLine(name + ": " + endpoint.MixFormat + (endpoint.Error == null ? "" : " / " + endpoint.Error));
                    }
                    catch (Exception e) { report.Errors.Add("Endpoint " + i + ": " + e.Message); }
                    finally { Release(device); }
                }
            }
            catch (Exception e) { report.Errors.Add("Core Audio: " + e.Message); }
            finally { Release(collection); Release(enumerator); }
            try
            {
                string json = new JavaScriptSerializer { MaxJsonLength = 4 * 1024 * 1024 }.Serialize(report);
                if (destination == null) Console.WriteLine(json);
                else
                {
                    string path = Path.GetFullPath(destination);
                    Directory.CreateDirectory(Path.GetDirectoryName(path));
                    File.WriteAllText(path, json, new UTF8Encoding(false));
                    Console.WriteLine("Report: " + path);
                }
            }
            catch (Exception e) { Console.Error.WriteLine("Writing report: " + e.Message); return 2; }
            foreach (string error in report.Errors) Console.Error.WriteLine(error);
            foreach (Endpoint endpoint in report.Endpoints) if (endpoint.Error != null) return 2;
            if (report.Errors.Count > 0) return 2;
            if (report.Endpoints.Count == 0) { Console.Error.WriteLine("No matching active playback endpoints. Check the filter and Device Manager."); return 3; }
            return 0;
        }

        private static void Usage()
        {
            Console.WriteLine("PhoebusProbe.exe [--json report.json] [--filter name-substring]");
            Console.WriteLine("Lists active playback endpoints and queries PCM format support. Changes no settings and plays no audio.");
        }

        private static Endpoint Inspect(IMMDevice device, string name)
        {
            Endpoint result = new Endpoint { Name = name };
            IAudioClient client = null;
            IntPtr mix = IntPtr.Zero;
            try
            {
                string id;
                Check(device.GetId(out id));
                result.Id = id;
                object disabled = Property(device, "1da5d803-d492-4edd-8c23-e0c0ffee7f0e", 5);
                result.SystemEffectsDisabled = disabled == null ? (bool?)null : Convert.ToUInt32(disabled) != 0;
                Guid iid = new Guid("1cb9ad4c-dbfa-4c32-b178-c2f568a703b2");
                object activated;
                Check(device.Activate(ref iid, 23, IntPtr.Zero, out activated));
                client = (IAudioClient)activated;
                Check(client.GetMixFormat(out mix));
                result.MixFormat = DescribeFormat(mix);
                long normal, minimum;
                Check(client.GetDevicePeriod(out normal, out minimum));
                result.DefaultPeriodMs = normal / 10000.0;
                result.MinimumPeriodMs = minimum / 10000.0;
                foreach (int rate in new int[] { 44100, 48000, 88200, 96000, 176400, 192000 })
                {
                    AddQuery(result, client, rate, 2, 16, 16, false);
                    AddQuery(result, client, rate, 2, 16, 16, true);
                    AddQuery(result, client, rate, 2, 24, 24, true);
                    AddQuery(result, client, rate, 2, 32, 24, true);
                }
                foreach (int channels in new int[] { 6, 8 })
                {
                    AddQuery(result, client, 48000, channels, 16, 16, true);
                    AddQuery(result, client, 48000, channels, 32, 24, true);
                }
            }
            catch (Exception e) { result.Error = e.Message; }
            finally { if (mix != IntPtr.Zero) Marshal.FreeCoTaskMem(mix); Release(client); }
            return result;
        }

        private static void AddQuery(Endpoint endpoint, IAudioClient client, int rate, int channels, int container, int valid, bool extensible)
        {
            byte[] bytes = PcmFormat(rate, channels, container, valid, extensible);
            IntPtr format = Marshal.AllocCoTaskMem(bytes.Length);
            try
            {
                Marshal.Copy(bytes, 0, format, bytes.Length);
                foreach (int mode in new int[] { 0, 1 })
                {
                    FormatQuery query = new FormatQuery { RateHz = rate, Channels = channels, ContainerBits = container,
                        ValidBits = valid, Representation = extensible ? "WAVEFORMATEXTENSIBLE" : "WAVEFORMATEX",
                        Mode = mode == 0 ? "shared" : "exclusive" };
                    IntPtr slot = IntPtr.Zero, closest = IntPtr.Zero;
                    try
                    {
                        // Shared mode needs WAVEFORMATEX**; exclusive mode must use NULL.
                        if (mode == 0) { slot = Marshal.AllocCoTaskMem(IntPtr.Size); Marshal.WriteIntPtr(slot, IntPtr.Zero); }
                        int hr = client.IsFormatSupported(mode, format, slot);
                        if (slot != IntPtr.Zero) closest = Marshal.ReadIntPtr(slot);
                        query.HResult = "0x" + unchecked((uint)hr).ToString("X8");
                        query.Status = hr == 0 ? "supported" : hr == 1 ? "closest-match" :
                            unchecked((uint)hr) == 0x88890008 ? "unsupported" : "error";
                        if (closest != IntPtr.Zero) query.ClosestFormat = DescribeFormat(closest);
                    }
                    finally
                    {
                        if (closest != IntPtr.Zero) Marshal.FreeCoTaskMem(closest);
                        if (slot != IntPtr.Zero) Marshal.FreeCoTaskMem(slot);
                    }
                    endpoint.Formats.Add(query);
                    if (query.Status == "error" && endpoint.Error == null)
                        endpoint.Error = "At least one format query failed; inspect its HRESULT in Formats.";
                }
            }
            finally { Marshal.FreeCoTaskMem(format); }
        }

        private static byte[] PcmFormat(int rate, int channels, int container, int valid, bool extensible)
        {
            using (MemoryStream stream = new MemoryStream())
            using (BinaryWriter writer = new BinaryWriter(stream))
            {
                writer.Write((ushort)(extensible ? 0xfffe : 1));
                writer.Write((ushort)channels);
                writer.Write((uint)rate);
                writer.Write((uint)(rate * channels * container / 8));
                writer.Write((ushort)(channels * container / 8));
                writer.Write((ushort)container);
                writer.Write((ushort)(extensible ? 22 : 0));
                if (extensible)
                {
                    writer.Write((ushort)valid);
                    writer.Write((uint)(channels == 2 ? 3 : channels == 6 ? 0x3f : 0x63f));
                    writer.Write(new Guid("00000001-0000-0010-8000-00aa00389b71").ToByteArray());
                }
                return stream.ToArray();
            }
        }

        private static string DescribeFormat(IntPtr format)
        {
            if (format == IntPtr.Zero) return "not exposed";
            ushort tag = unchecked((ushort)Marshal.ReadInt16(format));
            int channels = unchecked((ushort)Marshal.ReadInt16(format, 2));
            int rate = Marshal.ReadInt32(format, 4);
            int bits = unchecked((ushort)Marshal.ReadInt16(format, 14));
            string kind = tag == 1 ? "PCM" : tag == 3 ? "float" : "tag 0x" + tag.ToString("X4");
            if (tag == 0xfffe && unchecked((ushort)Marshal.ReadInt16(format, 16)) >= 22)
            {
                Guid subformat = (Guid)Marshal.PtrToStructure(IntPtr.Add(format, 24), typeof(Guid));
                kind = subformat == new Guid("00000001-0000-0010-8000-00aa00389b71") ? "PCM" :
                    subformat == new Guid("00000003-0000-0010-8000-00aa00389b71") ? "float" : subformat.ToString();
                kind += " (" + unchecked((ushort)Marshal.ReadInt16(format, 18)) + " valid bits)";
            }
            return rate + " Hz / " + channels + " channels / " + bits + " bits / " + kind;
        }

        private static object Property(IMMDevice device, string guid, uint pid)
        {
            IPropertyStore store = null;
            PropVariant value = new PropVariant();
            try
            {
                Check(device.OpenPropertyStore(0, out store)); // STGM_READ
                PropertyKey key = new PropertyKey { FormatId = new Guid(guid), Id = pid };
                Check(store.GetValue(ref key, out value));
                if (value.Type == 31) return Marshal.PtrToStringUni(value.Pointer);
                if (value.Type == 19) return value.Unsigned;
                return null;
            }
            finally { PropVariantClear(ref value); Release(store); }
        }

        private static void Check(int hr) { if (hr < 0) Marshal.ThrowExceptionForHR(hr); }
        private static void Release(object value) { if (value != null && Marshal.IsComObject(value)) Marshal.ReleaseComObject(value); }
        [DllImport("ole32.dll")] private static extern int PropVariantClear(ref PropVariant value);
    }

    public sealed class Report
    {
        public string CapturedAtUtc, WindowsBuild, Limitation;
        public List<Endpoint> Endpoints = new List<Endpoint>();
        public List<string> Errors = new List<string>();
    }
    public sealed class Endpoint
    {
        public string Name, Id, MixFormat, Error;
        public bool? SystemEffectsDisabled;
        public double DefaultPeriodMs, MinimumPeriodMs;
        public List<FormatQuery> Formats = new List<FormatQuery>();
    }
    public sealed class FormatQuery
    {
        public int RateHz, Channels, ContainerBits, ValidBits;
        public string Representation, Mode, Status, HResult, ClosestFormat;
    }
    [StructLayout(LayoutKind.Sequential)] internal struct PropertyKey { public Guid FormatId; public uint Id; }
    [StructLayout(LayoutKind.Explicit, Size = 24)] internal struct PropVariant
    {
        [FieldOffset(0)] public ushort Type;
        [FieldOffset(8)] public IntPtr Pointer;
        [FieldOffset(8)] public uint Unsigned;
    }
    [ComImport, Guid("bcde0395-e52f-467c-8e3d-c4579291692e")] internal class MMDeviceEnumerator { }
    [ComImport, Guid("a95664d2-9614-4f35-a746-de8db63617e6"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IMMDeviceEnumerator
    {
        [PreserveSig] int EnumAudioEndpoints(int flow, uint mask, out IMMDeviceCollection collection);
        [PreserveSig] int GetDefaultAudioEndpoint(int flow, int role, out IMMDevice device);
        [PreserveSig] int GetDevice([MarshalAs(UnmanagedType.LPWStr)] string id, out IMMDevice device);
        [PreserveSig] int RegisterEndpointNotificationCallback(IntPtr callback);
        [PreserveSig] int UnregisterEndpointNotificationCallback(IntPtr callback);
    }
    [ComImport, Guid("0bd7a1be-7a1a-44db-8397-cc5392387b5e"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IMMDeviceCollection
    {
        [PreserveSig] int GetCount(out uint count);
        [PreserveSig] int Item(uint index, out IMMDevice device);
    }
    [ComImport, Guid("d666063f-1587-4e43-81f1-b948e807363f"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IMMDevice
    {
        [PreserveSig] int Activate(ref Guid iid, uint context, IntPtr parameters, [MarshalAs(UnmanagedType.IUnknown)] out object value);
        [PreserveSig] int OpenPropertyStore(uint access, out IPropertyStore store);
        [PreserveSig] int GetId([MarshalAs(UnmanagedType.LPWStr)] out string id);
        [PreserveSig] int GetState(out uint state);
    }
    [ComImport, Guid("886d8eeb-8cf2-4446-8d02-cdba1dbdcf99"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IPropertyStore
    {
        [PreserveSig] int GetCount(out uint count);
        [PreserveSig] int GetAt(uint index, out PropertyKey key);
        [PreserveSig] int GetValue(ref PropertyKey key, out PropVariant value);
        [PreserveSig] int SetValue(ref PropertyKey key, ref PropVariant value);
        [PreserveSig] int Commit();
    }
    [ComImport, Guid("1cb9ad4c-dbfa-4c32-b178-c2f568a703b2"), InterfaceType(ComInterfaceType.InterfaceIsIUnknown)]
    internal interface IAudioClient
    {
        [PreserveSig] int Initialize(int mode, uint flags, long duration, long period, IntPtr format, IntPtr session);
        [PreserveSig] int GetBufferSize(out uint frames);
        [PreserveSig] int GetStreamLatency(out long latency);
        [PreserveSig] int GetCurrentPadding(out uint frames);
        [PreserveSig] int IsFormatSupported(int mode, IntPtr format, IntPtr closest);
        [PreserveSig] int GetMixFormat(out IntPtr format);
        [PreserveSig] int GetDevicePeriod(out long normal, out long minimum);
        [PreserveSig] int Start();
        [PreserveSig] int Stop();
        [PreserveSig] int Reset();
        [PreserveSig] int SetEventHandle(IntPtr handle);
        [PreserveSig] int GetService(ref Guid iid, [MarshalAs(UnmanagedType.IUnknown)] out object service);
    }
}
