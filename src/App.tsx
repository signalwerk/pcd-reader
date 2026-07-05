import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type ChangeEvent,
  type DragEvent,
} from "react";
import "./App.css";
import {
  convertPcd,
  preloadPcdCodec,
  PCD_RESOLUTIONS,
  DEFAULT_RESOLUTION,
  type PcdFormat,
  type WhiteBalance,
} from "./lib/pcdCodec";
import { downloadBlob } from "./lib/fileSave";

type QualityPreset = "standard" | "high" | "maximum" | "custom";

interface FileEntry {
  id: string;
  file: File;
  name: string;
  size: number;
  status: "pending" | "converting" | "done" | "error";
  error?: string;
  warning?: string;
  resultData?: Uint8Array;
  resultName?: string;
  resultFormat?: PcdFormat;
  width?: number;
  height?: number;
  saved?: boolean;
}

const QUALITY_MAP: Record<Exclude<QualityPreset, "custom">, number> = {
  standard: 85,
  high: 95,
  maximum: 100,
};

function formatBytes(bytes: number): string {
  if (bytes < 1024) return bytes + " B";
  if (bytes < 1024 * 1024) return (bytes / 1024).toFixed(1) + " KB";
  return (bytes / (1024 * 1024)).toFixed(1) + " MB";
}

function extensionFor(format: PcdFormat): string {
  return format === "tiff" ? ".tif" : ".jpg";
}

function mimeTypeFor(format: PcdFormat): string {
  return format === "tiff" ? "image/tiff" : "image/jpeg";
}

function replaceExtension(name: string, format: PcdFormat): string {
  return name.replace(/\.[^.]+$/, "") + extensionFor(format);
}

function isPcdFile(file: File): boolean {
  return file.name.toLowerCase().endsWith(".pcd");
}

// Translates the decoder's raw warning text into something a non-technical
// user can act on. Falls back to the raw message for anything unexpected
// (e.g. genuine data-recovery warnings) so nothing gets silently swallowed.
function explainWarning(raw: string): string {
  if (/ipe|64base/i.test(raw)) {
    return "The highest Photo CD resolution, 64Base, requires a separate IMAGE PAC Extension (IPE). This extension is only present on Photo CD Pro Master discs and is not included in standalone .pcd files. The converter has automatically fallen back to the next-best resolution, 16Base (2048 × 3072).";
  }
  return raw;
}

let idCounter = 0;

function App() {
  const [isReady, setIsReady] = useState(false);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [files, setFiles] = useState<FileEntry[]>([]);

  const [format, setFormat] = useState<PcdFormat>("tiff");
  const [qualityPreset, setQualityPreset] = useState<QualityPreset>("maximum");
  const [customQuality, setCustomQuality] = useState(90);
  const [resolution, setResolution] = useState(DEFAULT_RESOLUTION);
  const [whiteBalance, setWhiteBalance] = useState<WhiteBalance>("D65");
  const [monochrome, setMonochrome] = useState(false);

  const [converting, setConverting] = useState(false);
  const [dragActive, setDragActive] = useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const jpegQuality =
    qualityPreset === "custom" ? customQuality : QUALITY_MAP[qualityPreset];

  useEffect(() => {
    preloadPcdCodec()
      .then(() => setIsReady(true))
      .catch((err) => setLoadError(String(err)));
  }, []);

  // Settings changed after a conversion: invalidate finished/errored entries
  // so users don't accidentally save a result made with stale settings.
  useEffect(() => {
    setFiles((prev) => {
      const needsReset = prev.some(
        (f) => f.status === "done" || f.status === "error",
      );
      if (!needsReset) return prev;
      return prev.map((f) =>
        f.status === "done" || f.status === "error"
          ? {
              ...f,
              status: "pending",
              error: undefined,
              warning: undefined,
              resultData: undefined,
              resultName: undefined,
              resultFormat: undefined,
              width: undefined,
              height: undefined,
              saved: false,
            }
          : f,
      );
    });
  }, [
    format,
    qualityPreset,
    customQuality,
    resolution,
    whiteBalance,
    monochrome,
  ]);

  const addFiles = useCallback((fileList: FileList) => {
    const supported = Array.from(fileList).filter(isPcdFile);
    const entries: FileEntry[] = supported.map((f) => ({
      id: String(++idCounter),
      file: f,
      name: f.name,
      size: f.size,
      status: "pending" as const,
    }));
    setFiles((prev) => [...prev, ...entries]);
  }, []);

  const handleDrop = useCallback(
    (e: DragEvent) => {
      e.preventDefault();
      e.stopPropagation();
      setDragActive(false);
      if (e.dataTransfer.files.length) addFiles(e.dataTransfer.files);
    },
    [addFiles],
  );

  const handleDragOver = useCallback((e: DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setDragActive(true);
  }, []);

  const handleDragLeave = useCallback((e: DragEvent) => {
    e.preventDefault();
    e.stopPropagation();
    setDragActive(false);
  }, []);

  const handleFileInput = useCallback(
    (e: ChangeEvent<HTMLInputElement>) => {
      if (e.target.files?.length) addFiles(e.target.files);
      e.target.value = "";
    },
    [addFiles],
  );

  const convertAll = useCallback(async () => {
    setConverting(true);
    const pending = files.filter((f) => f.status === "pending");

    for (const entry of pending) {
      setFiles((prev) =>
        prev.map((f) =>
          f.id === entry.id ? { ...f, status: "converting" } : f,
        ),
      );

      try {
        const bytes = new Uint8Array(await entry.file.arrayBuffer());
        const result = await convertPcd(bytes, {
          format,
          resolution,
          whiteBalance,
          monochrome,
          jpegQuality,
        });
        const resultName = replaceExtension(entry.name, format);

        setFiles((prev) =>
          prev.map((f) =>
            f.id === entry.id
              ? {
                  ...f,
                  status: "done",
                  resultData: result.data,
                  resultName,
                  resultFormat: format,
                  width: result.width,
                  height: result.height,
                  warning: result.warning ?? undefined,
                  saved: false,
                }
              : f,
          ),
        );
      } catch (err) {
        setFiles((prev) =>
          prev.map((f) =>
            f.id === entry.id
              ? { ...f, status: "error", error: String(err) }
              : f,
          ),
        );
      }
    }

    setConverting(false);
  }, [files, format, resolution, whiteBalance, monochrome, jpegQuality]);

  const saveEntry = useCallback(async (entry: FileEntry) => {
    if (!entry.resultData || !entry.resultName || !entry.resultFormat) return;
    try {
      downloadBlob(
        entry.resultName,
        entry.resultData,
        mimeTypeFor(entry.resultFormat),
      );
      setFiles((prev) =>
        prev.map((f) => (f.id === entry.id ? { ...f, saved: true } : f)),
      );
    } catch (err) {
      setFiles((prev) =>
        prev.map((f) =>
          f.id === entry.id
            ? { ...f, status: "error", error: `Save failed: ${String(err)}` }
            : f,
        ),
      );
    }
  }, []);

  const saveAll = useCallback(async () => {
    const done = files.filter((f) => f.status === "done" && !f.saved);
    for (const entry of done) {
      await saveEntry(entry);
    }
  }, [files, saveEntry]);

  const clearAll = () => setFiles([]);
  const removeFile = (id: string) =>
    setFiles((prev) => prev.filter((f) => f.id !== id));
  const resetFile = (id: string) =>
    setFiles((prev) =>
      prev.map((f) =>
        f.id === id
          ? {
              ...f,
              status: "pending",
              error: undefined,
              warning: undefined,
              resultData: undefined,
              resultName: undefined,
              resultFormat: undefined,
              width: undefined,
              height: undefined,
              saved: false,
            }
          : f,
      ),
    );

  const pendingCount = files.filter((f) => f.status === "pending").length;
  const doneCount = files.filter((f) => f.status === "done").length;
  const unsavedDoneCount = files.filter(
    (f) => f.status === "done" && !f.saved,
  ).length;
  const totalCount = files.length;
  const progress =
    totalCount > 0 ? ((totalCount - pendingCount) / totalCount) * 100 : 0;

  // Shown once above the file list rather than repeated on every row -- the
  // common case (defaulting to 64Base) triggers the same warning for every
  // file converted from a standalone .pcd.
  const uniqueWarnings = Array.from(
    new Set(
      files
        .filter((f) => f.status === "done" && f.warning)
        .map((f) => explainWarning(f.warning as string)),
    ),
  );

  if (loadError) {
    return (
      <div className="app">
        <h1>PCD Converter</h1>
        <p className="error-banner">
          Failed to load the conversion engine: {loadError}
        </p>
      </div>
    );
  }

  if (!isReady) {
    return (
      <div
        className="app"
        style={{
          display: "flex",
          justifyContent: "center",
          alignItems: "center",
          height: "100vh",
        }}
      >
        <p
          style={{ fontSize: "1.2rem", fontFamily: "IBM Plex Mono, monospace" }}
        >
          Loading engine…
        </p>
      </div>
    );
  }

  return (
    <div className="app">
      <h1>Photo CD (PCD) → {format === "jpeg" ? "JPEG" : "TIFF"}</h1>
      <p style={{ marginBottom: "1.5em" }}>
        Drop your Kodak Photo CD (.pcd) files below to convert them to a
        color-managed, ICC-tagged JPEG or TIFF. Everything runs in your browser
        via WebAssembly — no files are uploaded anywhere.
      </p>

      {/* Drop Zone */}
      <div
        className={`drop-zone ${dragActive ? "drop-zone--active" : ""}`}
        onDrop={handleDrop}
        onDragOver={handleDragOver}
        onDragLeave={handleDragLeave}
      >
        <input
          ref={fileInputRef}
          type="file"
          accept=".pcd"
          multiple
          onChange={handleFileInput}
        />
        <p>
          <strong>Drop .PCD files here</strong>
          <br />
          or click to browse
        </p>
      </div>

      {/* Settings */}
      <div className="settings">
        <h2 style={{ marginBottom: "0.5em" }}>Conversion Settings</h2>

        <div className="setting-group">
          <label htmlFor="format">Output Format</label>
          <select
            id="format"
            value={format}
            onChange={(e) => setFormat(e.target.value as PcdFormat)}
          >
            <option value="jpeg">JPEG</option>
            <option value="tiff">TIFF (ZIP compressed)</option>
          </select>
        </div>

        <div className="setting-group">
          <label htmlFor="resolution">Resolution</label>
          <select
            id="resolution"
            value={resolution}
            onChange={(e) => setResolution(Number(e.target.value))}
          >
            {PCD_RESOLUTIONS.map((r) => (
              <option key={r.value} value={r.value}>
                {r.label}
              </option>
            ))}
          </select>
        </div>

        {format === "jpeg" && (
          <div className="setting-group">
            <label htmlFor="quality">JPEG Quality</label>
            <select
              id="quality"
              value={qualityPreset}
              onChange={(e) =>
                setQualityPreset(e.target.value as QualityPreset)
              }
            >
              <option value="standard">Standard (85%)</option>
              <option value="high">High (95%)</option>
              <option value="maximum">Maximum (100%)</option>
              <option value="custom">Custom</option>
            </select>
            {qualityPreset === "custom" && (
              <div className="quality-custom">
                <input
                  type="number"
                  min={1}
                  max={100}
                  value={customQuality}
                  onChange={(e) =>
                    setCustomQuality(
                      Math.max(1, Math.min(100, Number(e.target.value))),
                    )
                  }
                  style={{ display: "inline-block", width: "5em" }}
                />
                <span style={{ marginLeft: "0.4em" }}>%</span>
              </div>
            )}
          </div>
        )}

        <div className="setting-group">
          <label htmlFor="whiteBalance">White Balance</label>
          <select
            id="whiteBalance"
            value={whiteBalance}
            onChange={(e) => setWhiteBalance(e.target.value as WhiteBalance)}
          >
            <option value="D65">D65 — 6500K (standard PhotoCD scan)</option>
            <option value="D50">D50 — 5000K</option>
          </select>
        </div>

        <div className="setting-group setting-group--checkbox">
          <label htmlFor="monochrome" className="checkbox-label">
            <input
              id="monochrome"
              type="checkbox"
              checked={monochrome}
              onChange={(e) => setMonochrome(e.target.checked)}
            />
            Process as monochrome
          </label>
        </div>
      </div>

      {/* Actions */}
      <div className="actions">
        <button
          className="btn btn--primary"
          disabled={pendingCount === 0 || converting}
          onClick={convertAll}
        >
          {converting
            ? "Converting…"
            : `Convert ${pendingCount} file${pendingCount !== 1 ? "s" : ""}`}
        </button>
        <button
          className="btn"
          disabled={unsavedDoneCount === 0}
          onClick={saveAll}
        >
          {`Download all (${unsavedDoneCount})`}
        </button>
        <button className="btn" disabled={totalCount === 0} onClick={clearAll}>
          Clear
        </button>
      </div>

      {/* Progress */}
      {converting && (
        <div className="progress-bar">
          <div
            className="progress-bar__fill"
            style={{ width: `${progress}%` }}
          />
        </div>
      )}

      {/* Notices: shown once, deduplicated, rather than repeated per file */}
      {uniqueWarnings.length > 0 && (
        <div className="notice-banner">
          {uniqueWarnings.map((w, i) => (
            <p key={i}>ℹ️ {w}</p>
          ))}
        </div>
      )}

      {/* File List */}
      {files.length > 0 && (
        <div className="file-list">
          {files.map((entry) => (
            <div className="file-item" key={entry.id}>
              <div className="file-item__info">
                <span className="file-item__name" title={entry.name}>
                  {entry.name}
                </span>
                <span className="file-item__size">
                  {formatBytes(entry.size)}
                  {entry.width && entry.height
                    ? ` · ${entry.width}×${entry.height}`
                    : ""}
                </span>
                {entry.status === "error" && (
                  <span className="file-item__error-message">
                    {entry.error}
                  </span>
                )}
              </div>
              <span
                className={`file-item__status file-item__status--${entry.status}`}
              >
                {entry.status === "pending" && "Pending"}
                {entry.status === "converting" && "Converting…"}
                {entry.status === "done" &&
                  `${entry.saved ? "✓ Saved" : "✓ Converted"}${
                    entry.resultData
                      ? ` (${formatBytes(entry.resultData.length)})`
                      : ""
                  }`}
                {entry.status === "error" && "✗ Error"}
              </span>
              <div className="file-item__actions">
                {entry.status === "done" && !entry.saved && (
                  <button className="btn" onClick={() => saveEntry(entry)}>
                    Download
                  </button>
                )}
                {(entry.status === "done" || entry.status === "error") && (
                  <button
                    className="btn"
                    onClick={() => resetFile(entry.id)}
                    title="Reset to pending"
                  >
                    Reset
                  </button>
                )}
                <button className="btn" onClick={() => removeFile(entry.id)}>
                  ✕
                </button>
              </div>
            </div>
          ))}
        </div>
      )}

      {/* Summary */}
      {files.length > 0 && (
        <div className="summary">
          {totalCount} file{totalCount !== 1 ? "s" : ""} · {doneCount} converted
          · {pendingCount} pending
        </div>
      )}

      <footer className="app-footer">
        <a
          href="https://github.com/signalwerk/pcd-reader"
          target="_blank"
          rel="noopener noreferrer"
        >
          Source code
        </a>
        <span className="app-footer__sep">·</span>
        <a
          href="https://sourceforge.net/projects/pcdtojpeg/"
          target="_blank"
          rel="noopener noreferrer"
        >
          pcdtojpeg library
        </a>
      </footer>
    </div>
  );
}

export default App;
