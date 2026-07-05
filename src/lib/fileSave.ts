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
