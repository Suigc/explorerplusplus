param([Parameter(Mandatory)][string]$OutputPath)

$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
public static class WindowsFolderIconExport {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32.dll")] private static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll")] private static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")] private static extern IntPtr LockResource(IntPtr resource);
    [DllImport("kernel32.dll")] private static extern uint SizeofResource(IntPtr module, IntPtr resource);
    private static byte[] Read(IntPtr module, int type, int name) {
        IntPtr resource = FindResource(module, new IntPtr(name), new IntPtr(type));
        if (resource == IntPtr.Zero) throw new Win32Exception();
        byte[] bytes = new byte[SizeofResource(module, resource)];
        Marshal.Copy(LockResource(LoadResource(module, resource)), bytes, 0, bytes.Length);
        return bytes;
    }
    public static byte[] Export(string library) {
        IntPtr module = LoadLibraryEx(library, IntPtr.Zero, 0x22);
        if (module == IntPtr.Zero) throw new Win32Exception();
        try {
            byte[] group = Read(module, 14, 3);
            int count = BitConverter.ToUInt16(group, 4);
            byte[][] images = new byte[count][];
            for (int i = 0; i < count; ++i)
                images[i] = Read(module, 3, BitConverter.ToUInt16(group, 6 + i * 14 + 12));
            using (var output = new MemoryStream())
            using (var writer = new BinaryWriter(output)) {
                writer.Write(group, 0, 6);
                int offset = 6 + count * 16;
                for (int i = 0; i < count; ++i) {
                    writer.Write(group, 6 + i * 14, 8);
                    writer.Write(images[i].Length);
                    writer.Write(offset);
                    offset += images[i].Length;
                }
                foreach (byte[] image in images) writer.Write(image);
                return output.ToArray();
            }
        } finally { FreeLibrary(module); }
    }
}
'@
$library = Join-Path $env:SystemRoot 'SystemResources\imageres.dll.mun'
if (!(Test-Path -LiteralPath $library)) { $library = Join-Path $env:SystemRoot 'System32\imageres.dll' }
$destination = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPath)
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
[IO.File]::WriteAllBytes($destination, [WindowsFolderIconExport]::Export($library))
Get-Item -LiteralPath $destination | Select-Object FullName,Length
