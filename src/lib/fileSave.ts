// Saves converted files either directly to a user-chosen folder via the File
// System Access API, or by triggering a browser download when that API isn't
// available (Firefox, Safari, and any non-secure-context / non-Chromium
// browser as of this writing).

export function isFileSystemAccessSupported(): boolean {
  return typeof window !== "undefined" && "showDirectoryPicker" in window;
}

/** Returns null if the user cancelled the picker. */
export async function pickDestinationDirectory(): Promise<FileSystemDirectoryHandle | null> {
  try {
    return await window.showDirectoryPicker({ mode: "readwrite" });
  } catch (err) {
    if (err instanceof DOMException && err.name === "AbortError") return null;
    throw err;
  }
}

/** Re-requests write permission for a directory handle restored from a previous session (not currently persisted, but kept isolated here for when it is). */
export async function ensureWritePermission(dirHandle: FileSystemDirectoryHandle): Promise<boolean> {
  const opts = { mode: "readwrite" as const };
  if ((await dirHandle.queryPermission(opts)) === "granted") return true;
  return (await dirHandle.requestPermission(opts)) === "granted";
}

export async function saveToDirectory(
  dirHandle: FileSystemDirectoryHandle,
  filename: string,
  data: Uint8Array
): Promise<void> {
  const fileHandle = await dirHandle.getFileHandle(filename, { create: true });
  const writable = await fileHandle.createWritable();
  await writable.write(data as BufferSource);
  await writable.close();
}

export function downloadBlob(filename: string, data: Uint8Array, mimeType: string): void {
  const blob = new Blob([data as BlobPart], { type: mimeType });
  const url = URL.createObjectURL(blob);
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  document.body.appendChild(anchor);
  anchor.click();
  anchor.remove();
  // Give the download a moment to start before revoking the object URL.
  setTimeout(() => URL.revokeObjectURL(url), 2000);
}
