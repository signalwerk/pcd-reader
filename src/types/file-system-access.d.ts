// Parts of the File System Access API that lib.dom.d.ts doesn't (yet) ship:
// the directory picker entry point and the read/write permission handshake.
// Everything else (FileSystemDirectoryHandle.getFileHandle,
// FileSystemFileHandle.createWritable, etc.) is already covered by lib.dom.

export {};

type PermissionStateExtended = "granted" | "denied" | "prompt";

interface FileSystemHandlePermissionDescriptor {
  mode?: "read" | "readwrite";
}

declare global {
  interface FileSystemDirectoryHandle {
    queryPermission(descriptor?: FileSystemHandlePermissionDescriptor): Promise<PermissionStateExtended>;
    requestPermission(descriptor?: FileSystemHandlePermissionDescriptor): Promise<PermissionStateExtended>;
  }

  interface DirectoryPickerOptions {
    mode?: "read" | "readwrite";
  }

  interface Window {
    showDirectoryPicker(options?: DirectoryPickerOptions): Promise<FileSystemDirectoryHandle>;
  }
}
