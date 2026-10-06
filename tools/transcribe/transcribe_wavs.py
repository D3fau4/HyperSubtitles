from __future__ import annotations

import argparse
import csv
import json
import struct
import sys
import time
import unicodedata
from pathlib import Path


TARGET_SAMPLE_RATE = 16000
DEFAULT_SKIP_DIRS = ("BGM", "SFX", "JINGLE")
WAVE_FORMAT_PCM = 0x0001
WAVE_FORMAT_ADPCM = 0x0002
WAVE_FORMAT_IEEE_FLOAT = 0x0003
WAVE_FORMAT_EXTENSIBLE = 0xFFFE
LANGUAGE_ALIASES = {
    "jp": "ja",
    "ja-jp": "ja",
    "ja_jp": "ja",
    "japanese": "ja",
    "japan": "ja",
    "japones": "ja",
    "nihongo": "ja",
    "\u65e5\u672c\u8a9e": "ja",
}

MS_ADPCM_ADAPTATION_TABLE = (230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230)
MS_ADPCM_DEFAULT_COEFFICIENTS = (
    (256, 0),
    (512, -256),
    (0, 0),
    (192, 64),
    (240, 0),
    (460, -208),
    (392, -232),
)


def decode_pcm(raw: bytes, sample_width: int):
    import numpy as np

    if sample_width == 1:
        data = np.frombuffer(raw, dtype=np.uint8).astype(np.float32)
        return (data - 128.0) / 128.0

    if sample_width == 2:
        data = np.frombuffer(raw, dtype="<i2").astype(np.float32)
        return data / 32768.0

    if sample_width == 3:
        data = np.frombuffer(raw, dtype=np.uint8)
        remainder = data.size % 3
        if remainder:
            data = data[: data.size - remainder]
        triples = data.reshape(-1, 3).astype(np.int32)
        values = triples[:, 0] | (triples[:, 1] << 8) | (triples[:, 2] << 16)
        values = np.where(values >= 0x800000, values - 0x1000000, values)
        return values.astype(np.float32) / 8388608.0

    if sample_width == 4:
        data = np.frombuffer(raw, dtype="<i4").astype(np.float32)
        return data / 2147483648.0

    raise ValueError(f"Profundidad WAV no soportada: {sample_width * 8} bits")


def decode_ieee_float(raw: bytes, sample_width: int):
    import numpy as np

    if sample_width == 4:
        return np.frombuffer(raw, dtype="<f4").astype(np.float32, copy=False)
    if sample_width == 8:
        return np.frombuffer(raw, dtype="<f8").astype(np.float32)
    raise ValueError(f"Profundidad float WAV no soportada: {sample_width * 8} bits")


def resample_linear(audio, source_rate: int):
    import numpy as np

    if source_rate == TARGET_SAMPLE_RATE:
        return audio.astype(np.float32, copy=False)
    if source_rate <= 0:
        raise ValueError(f"Sample rate invalido: {source_rate}")
    if audio.size == 0:
        return audio.astype(np.float32, copy=False)

    output_len = max(1, int(round(audio.size * TARGET_SAMPLE_RATE / source_rate)))
    source_positions = np.arange(output_len, dtype=np.float64) * (source_rate / TARGET_SAMPLE_RATE)
    source_positions = np.minimum(source_positions, audio.size - 1)
    resampled = np.interp(source_positions, np.arange(audio.size), audio)
    return resampled.astype(np.float32)


def riff_chunks(path: Path):
    data = path.read_bytes()
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise RuntimeError("No es un archivo RIFF/WAVE")

    chunks: dict[bytes, list[bytes]] = {}
    offset = 12
    while offset + 8 <= len(data):
        chunk_id = data[offset : offset + 4]
        chunk_size = struct.unpack_from("<I", data, offset + 4)[0]
        start = offset + 8
        end = start + chunk_size
        if end > len(data):
            raise RuntimeError(f"Chunk WAV truncado: {chunk_id!r}")
        chunks.setdefault(chunk_id, []).append(data[start:end])
        offset = end + (chunk_size % 2)
    return chunks


def parse_wave_format(fmt: bytes) -> dict:
    if len(fmt) < 16:
        raise RuntimeError("Chunk fmt demasiado corto")

    format_tag, channels, sample_rate, avg_bytes_per_sec, block_align, bits_per_sample = struct.unpack_from(
        "<HHIIHH", fmt, 0
    )
    cb_size = struct.unpack_from("<H", fmt, 16)[0] if len(fmt) >= 18 else 0

    effective_format_tag = format_tag
    if format_tag == WAVE_FORMAT_EXTENSIBLE and len(fmt) >= 40:
        effective_format_tag = struct.unpack_from("<H", fmt, 24)[0]

    info = {
        "format_tag": format_tag,
        "effective_format_tag": effective_format_tag,
        "channels": channels,
        "sample_rate": sample_rate,
        "avg_bytes_per_sec": avg_bytes_per_sec,
        "block_align": block_align,
        "bits_per_sample": bits_per_sample,
        "cb_size": cb_size,
        "samples_per_block": None,
        "coefficients": list(MS_ADPCM_DEFAULT_COEFFICIENTS),
    }

    if effective_format_tag == WAVE_FORMAT_ADPCM:
        if len(fmt) >= 20:
            info["samples_per_block"] = struct.unpack_from("<H", fmt, 18)[0]
        if len(fmt) >= 22:
            coefficient_count = struct.unpack_from("<H", fmt, 20)[0]
            coefficient_bytes = 22 + coefficient_count * 4
            if coefficient_count and coefficient_bytes <= len(fmt):
                info["coefficients"] = [
                    struct.unpack_from("<hh", fmt, 22 + index * 4) for index in range(coefficient_count)
                ]
        if not info["samples_per_block"]:
            if channels == 1:
                info["samples_per_block"] = ((block_align - 7) * 2) + 2
            else:
                info["samples_per_block"] = ((block_align - 7 * channels) * 2 // channels) + 2

    return info


def signed_nibble(nibble: int) -> int:
    return nibble - 16 if nibble & 0x08 else nibble


def clamp_int16(value: int) -> int:
    return max(-32768, min(32767, value))


def decode_ms_adpcm_sample(nibble: int, state: dict, coefficient: tuple[int, int]) -> int:
    prediction = ((state["sample1"] * coefficient[0]) + (state["sample2"] * coefficient[1])) // 256
    sample = clamp_int16(prediction + signed_nibble(nibble) * state["delta"])

    state["sample2"] = state["sample1"]
    state["sample1"] = sample
    state["delta"] = max(16, (MS_ADPCM_ADAPTATION_TABLE[nibble] * state["delta"]) // 256)
    return sample


def decode_ms_adpcm_block(block: bytes, fmt: dict):
    import numpy as np

    channels = int(fmt["channels"])
    samples_per_block = int(fmt["samples_per_block"])
    coefficients = fmt["coefficients"]
    header_size = channels * 7

    if channels not in (1, 2):
        raise RuntimeError(f"MS ADPCM con {channels} canales no soportado")
    if len(block) < header_size:
        raise RuntimeError("Bloque MS ADPCM truncado")

    offset = 0
    predictors = list(block[offset : offset + channels])
    offset += channels
    deltas = list(struct.unpack_from("<" + "h" * channels, block, offset))
    offset += 2 * channels
    samples1 = list(struct.unpack_from("<" + "h" * channels, block, offset))
    offset += 2 * channels
    samples2 = list(struct.unpack_from("<" + "h" * channels, block, offset))
    offset += 2 * channels

    states = []
    for channel in range(channels):
        predictor = predictors[channel]
        if predictor >= len(coefficients):
            predictor = 0
        states.append(
            {
                "coefficient": coefficients[predictor],
                "delta": max(16, abs(deltas[channel])),
                "sample1": samples1[channel],
                "sample2": samples2[channel],
            }
        )

    decoded = [[samples2[channel], samples1[channel]] for channel in range(channels)]

    if channels == 1:
        for byte in block[offset:]:
            for nibble in (byte >> 4, byte & 0x0F):
                if len(decoded[0]) >= samples_per_block:
                    break
                decoded[0].append(decode_ms_adpcm_sample(nibble, states[0], states[0]["coefficient"]))
    else:
        for byte in block[offset:]:
            if len(decoded[0]) < samples_per_block:
                decoded[0].append(decode_ms_adpcm_sample(byte >> 4, states[0], states[0]["coefficient"]))
            if len(decoded[1]) < samples_per_block:
                decoded[1].append(decode_ms_adpcm_sample(byte & 0x0F, states[1], states[1]["coefficient"]))
            if len(decoded[0]) >= samples_per_block and len(decoded[1]) >= samples_per_block:
                break

    channel_arrays = [np.asarray(channel_samples[:samples_per_block], dtype=np.float32) for channel_samples in decoded]
    if len(channel_arrays) == 1:
        mono = channel_arrays[0]
    else:
        min_len = min(array.size for array in channel_arrays)
        mono = np.mean([array[:min_len] for array in channel_arrays], axis=0)
    return mono / 32768.0


def decode_ms_adpcm(data: bytes, fmt: dict):
    import numpy as np

    block_align = int(fmt["block_align"])
    if block_align <= 0:
        raise RuntimeError("block_align invalido en MS ADPCM")

    blocks = []
    for offset in range(0, len(data), block_align):
        block = data[offset : offset + block_align]
        if len(block) < int(fmt["channels"]) * 7:
            continue
        blocks.append(decode_ms_adpcm_block(block, fmt))

    if not blocks:
        raise RuntimeError("No se pudieron decodificar bloques MS ADPCM")
    return np.concatenate(blocks).astype(np.float32, copy=False)


def load_wave_for_whisper(path: Path):
    import numpy as np

    chunks = riff_chunks(path)
    fmt_chunks = chunks.get(b"fmt ")
    data_chunks = chunks.get(b"data")
    if not fmt_chunks or not data_chunks:
        raise RuntimeError("WAV sin chunks fmt/data")

    fmt = parse_wave_format(fmt_chunks[0])
    channels = int(fmt["channels"])
    sample_width = int(fmt["bits_per_sample"]) // 8
    sample_rate = int(fmt["sample_rate"])
    format_tag = int(fmt["effective_format_tag"])
    raw = b"".join(data_chunks)

    if channels < 1:
        raise RuntimeError("WAV sin canales de audio")

    if format_tag == WAVE_FORMAT_PCM:
        audio = decode_pcm(raw, sample_width)
    elif format_tag == WAVE_FORMAT_IEEE_FLOAT:
        audio = decode_ieee_float(raw, sample_width)
    elif format_tag == WAVE_FORMAT_ADPCM:
        audio = decode_ms_adpcm(raw, fmt)
        return resample_linear(np.clip(audio, -1.0, 1.0), sample_rate)
    else:
        raise RuntimeError(f"Codec WAV no soportado: format tag 0x{format_tag:04X}")

    if channels > 1:
        usable = (audio.size // channels) * channels
        audio = audio[:usable].reshape(-1, channels).mean(axis=1)

    audio = np.clip(audio, -1.0, 1.0)
    return resample_linear(audio, sample_rate)


def iter_wavs(input_path: Path, skip_dirs: set[str]):
    if input_path.is_file():
        if input_path.suffix.lower() == ".wav":
            yield input_path
        return

    for path in sorted(input_path.rglob("*.wav")):
        relative_parts = [part.lower() for part in path.relative_to(input_path).parts[:-1]]
        if any(part in skip_dirs for part in relative_parts):
            continue
        yield path


def format_srt_timestamp(seconds: float) -> str:
    milliseconds_total = int(round(max(0.0, seconds) * 1000))
    milliseconds = milliseconds_total % 1000
    seconds_total = milliseconds_total // 1000
    secs = seconds_total % 60
    minutes_total = seconds_total // 60
    mins = minutes_total % 60
    hours = minutes_total // 60
    return f"{hours:02}:{mins:02}:{secs:02},{milliseconds:03}"


def write_srt(segments: list[dict], path: Path) -> None:
    lines: list[str] = []
    for index, segment in enumerate(segments, start=1):
        text = str(segment.get("text", "")).strip()
        if not text:
            continue
        start = format_srt_timestamp(float(segment.get("start", 0.0)))
        end = format_srt_timestamp(float(segment.get("end", 0.0)))
        lines.extend([str(index), f"{start} --> {end}", text, ""])
    path.write_text("\n".join(lines), encoding="utf-8")


def po_escape(value: str) -> str:
    return (
        value.replace("\\", "\\\\")
        .replace('"', '\\"')
        .replace("\t", "\\t")
        .replace("\r\n", "\n")
        .replace("\r", "\n")
        .replace("\n", "\\n")
        .replace("\x00", "")
    )


def game_audio_path(wav_path: Path, input_root: Path) -> str:
    if input_root.is_file():
        relative = Path(wav_path.name)
    else:
        relative = wav_path.resolve().relative_to(input_root.resolve())
    return "/" + relative.with_suffix(".hca").as_posix()


def resolve_po_path(po_arg: str | None, output_root: Path) -> Path | None:
    if po_arg is None:
        return None
    if po_arg == "":
        return output_root / "transcripts.po"
    return Path(po_arg).resolve()


def write_po(rows: list[dict[str, str]], po_path: Path, input_root: Path) -> tuple[int, int]:
    po_path.parent.mkdir(parents=True, exist_ok=True)
    timestamp = time.strftime("%Y-%m-%d %H:%M%z")
    lines = [
        'msgid ""',
        'msgstr ""',
        f'"Project-Id-Version: wav-transcripts\\n"',
        f'"POT-Creation-Date: {timestamp}\\n"',
        '"Content-Type: text/plain; charset=UTF-8\\n"',
        '"Content-Transfer-Encoding: 8bit\\n"',
        '"Generated-By: transcribe_wavs.py\\n"',
        "",
    ]

    written = 0
    skipped_empty = 0
    seen: set[tuple[str, str]] = set()
    for row in rows:
        if row.get("status") not in {"ok", "skipped"}:
            continue

        text = row.get("text", "").strip()
        if not text:
            txt_path = row.get("txt", "")
            if txt_path and Path(txt_path).exists():
                text = Path(txt_path).read_text(encoding="utf-8", errors="replace").strip()

        if not text:
            skipped_empty += 1
            continue

        context = game_audio_path(Path(row["wav"]), input_root)
        key = (context, text)
        if key in seen:
            continue
        seen.add(key)

        lines.extend(
            [
                f"#: {context}",
                f'msgctxt "{po_escape(context)}"',
                f'msgid "{po_escape(text)}"',
                'msgstr ""',
                "",
            ]
        )
        written += 1

    po_path.write_text("\n".join(lines), encoding="utf-8")
    return written, skipped_empty


def normalize_language(language: str | None) -> str | None:
    if language is None:
        return None

    key = language.strip().lower()
    if not key:
        return None

    ascii_key = "".join(
        char for char in unicodedata.normalize("NFKD", key) if not unicodedata.combining(char)
    )
    return LANGUAGE_ALIASES.get(ascii_key, LANGUAGE_ALIASES.get(key, key))


def transcript_paths(output_root: Path, input_root: Path, wav_path: Path):
    if input_root.is_file():
        relative = wav_path.name
    else:
        relative = wav_path.relative_to(input_root)
    base = output_root / relative
    return base.with_suffix(".txt"), base.with_suffix(".json"), base.with_suffix(".srt")


def write_outputs(result: dict, txt_path: Path, json_path: Path, srt_path: Path, write_json: bool, write_srt_file: bool) -> None:
    txt_path.parent.mkdir(parents=True, exist_ok=True)
    txt_path.write_text(str(result.get("text", "")).strip() + "\n", encoding="utf-8")

    if write_json:
        json_path.parent.mkdir(parents=True, exist_ok=True)
        json_path.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")

    if write_srt_file:
        srt_path.parent.mkdir(parents=True, exist_ok=True)
        write_srt(result.get("segments", []), srt_path)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Transcribe WAV PCM o ACM/MS ADPCM con OpenAI Whisper.")
    parser.add_argument("-i", "--input", type=Path, default=Path("wav_out"), help="Archivo o carpeta WAV de entrada.")
    parser.add_argument("-o", "--output", type=Path, default=Path("transcripts"), help="Carpeta de salida.")
    parser.add_argument("--model", default="small", help="Modelo Whisper: tiny, base, small, medium, large, turbo...")
    parser.add_argument(
        "--language",
        default=None,
        help="Codigo de idioma, por ejemplo es o ja. Tambien acepta japanese/japones. Si se omite, Whisper detecta.",
    )
    parser.add_argument("--task", choices=("transcribe", "translate"), default="transcribe")
    parser.add_argument("--device", default="auto", help="auto, cpu, cuda o cuda:0.")
    parser.add_argument(
        "--fp16",
        action=argparse.BooleanOptionalAction,
        default=None,
        help="Usar FP16. Por defecto se activa en CUDA y se desactiva en CPU.",
    )
    parser.add_argument("--temperature", type=float, default=0.0)
    parser.add_argument("--beam-size", type=int, default=None, help="1 es mas rapido; valores mayores pueden mejorar precision.")
    parser.add_argument("--best-of", type=int, default=None)
    parser.add_argument("--initial-prompt", default=None)
    parser.add_argument("--skip-dirs", nargs="*", default=list(DEFAULT_SKIP_DIRS), help="Subcarpetas a saltar.")
    parser.add_argument("--include-all", action="store_true", help="No saltar BGM/SFX/JINGLE.")
    parser.add_argument("--limit", type=int, default=None, help="Procesar solo los primeros N WAV.")
    parser.add_argument("--force", action="store_true", help="Rehacer transcripciones existentes.")
    parser.add_argument("--dry-run", action="store_true", help="Listar archivos sin cargar Whisper.")
    parser.add_argument("--json", dest="write_json", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--srt", dest="write_srt", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument(
        "--po",
        nargs="?",
        const="",
        default=None,
        metavar="PATH",
        help="Exportar un unico .po. Sin PATH escribe transcripts.po dentro de la carpeta de salida.",
    )
    parser.add_argument("--verbose", action="store_true")
    return parser.parse_args()


def resolve_torch_runtime(device_arg: str, fp16_arg: bool | None):
    import torch

    cuda_available = torch.cuda.is_available()
    if device_arg == "auto":
        device = "cuda" if cuda_available else "cpu"
    else:
        device = device_arg

    uses_cuda = device.startswith("cuda")
    fp16 = uses_cuda if fp16_arg is None else fp16_arg

    if uses_cuda and not cuda_available:
        raise RuntimeError("Se pidio --device cuda, pero torch.cuda.is_available() es False")
    if not uses_cuda and fp16:
        print("FP16 no es recomendable en CPU; uso --no-fp16.")
        fp16 = False

    if uses_cuda:
        torch.backends.cuda.matmul.allow_tf32 = True
        torch.backends.cudnn.allow_tf32 = True

    cuda_runtime = torch.version.cuda or "no"
    gpu_name = torch.cuda.get_device_name(0) if cuda_available else "no"
    return device, fp16, cuda_runtime, gpu_name


def main() -> int:
    args = parse_args()
    language = normalize_language(args.language)
    input_path = args.input.resolve()
    output_root = args.output.resolve()
    skip_dirs = set() if args.include_all else {name.lower() for name in args.skip_dirs}

    if not input_path.exists():
        print(f"No existe la entrada: {input_path}", file=sys.stderr)
        return 2

    wavs = list(iter_wavs(input_path, skip_dirs))
    if args.limit is not None:
        wavs = wavs[: args.limit]

    if not wavs:
        print("No se encontraron WAV para transcribir.")
        return 0

    print(f"WAV encontrados: {len(wavs)}")
    if skip_dirs:
        print("Saltando carpetas:", ", ".join(sorted(skip_dirs)))

    if args.dry_run:
        for path in wavs:
            print(path)
        return 0

    model = None
    fp16 = False

    def ensure_model_loaded():
        nonlocal model, fp16
        if model is not None:
            return model, fp16

        try:
            import whisper
            import torch
        except ImportError:
            print("Falta openai-whisper. Ejecuta setup.cmd primero.", file=sys.stderr)
            raise

        device, fp16, cuda_runtime, gpu_name = resolve_torch_runtime(args.device, args.fp16)
        print(f"PyTorch: {torch.__version__} | CUDA runtime: {cuda_runtime} | GPU: {gpu_name}")
        print(f"Runtime Whisper: device={device}, fp16={fp16}")
        print(f"Idioma Whisper: {language or 'auto'}")
        print(f"Cargando modelo Whisper: {args.model}")
        model = whisper.load_model(args.model, device=device)
        return model, fp16

    output_root.mkdir(parents=True, exist_ok=True)
    rows: list[dict[str, str]] = []
    started_run = time.time()

    for index, wav_path in enumerate(wavs, start=1):
        txt_path, json_path, srt_path = transcript_paths(output_root, input_path, wav_path)
        if txt_path.exists() and not args.force:
            print(f"[{index}/{len(wavs)}] Ya existe, salto: {wav_path}")
            existing_text = txt_path.read_text(encoding="utf-8", errors="replace").strip()
            rows.append({"status": "skipped", "wav": str(wav_path), "txt": str(txt_path), "text": existing_text, "error": ""})
            continue

        print(f"[{index}/{len(wavs)}] Transcribiendo: {wav_path}")
        item_started = time.time()
        try:
            model, fp16 = ensure_model_loaded()
            audio = load_wave_for_whisper(wav_path)
            if audio.size == 0:
                raise RuntimeError("Audio vacio")

            transcribe_options = {
                "language": language,
                "task": args.task,
                "fp16": fp16,
                "temperature": args.temperature,
                "initial_prompt": args.initial_prompt,
                "verbose": args.verbose,
            }
            if args.beam_size is not None:
                transcribe_options["beam_size"] = args.beam_size
            if args.best_of is not None:
                transcribe_options["best_of"] = args.best_of

            result = model.transcribe(audio, **transcribe_options)
            write_outputs(result, txt_path, json_path, srt_path, args.write_json, args.write_srt)
            rows.append(
                {
                    "status": "ok",
                    "wav": str(wav_path),
                    "txt": str(txt_path),
                    "json": str(json_path) if args.write_json else "",
                    "srt": str(srt_path) if args.write_srt else "",
                    "seconds": f"{time.time() - item_started:.1f}",
                    "text": str(result.get("text", "")).strip(),
                    "error": "",
                }
            )
        except Exception as exc:
            print(f"ERROR en {wav_path}: {exc}", file=sys.stderr)
            rows.append({"status": "error", "wav": str(wav_path), "txt": str(txt_path), "error": str(exc)})

    index_path = output_root / "index.csv"
    fieldnames = ["status", "wav", "txt", "json", "srt", "seconds", "text", "error"]
    with index_path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)

    po_path = resolve_po_path(args.po, output_root)
    if po_path is not None:
        po_count, po_empty_count = write_po(rows, po_path, input_path)
        print(f"PO exportado: {po_path} ({po_count} entradas, {po_empty_count} vacias omitidas)")

    ok_count = sum(1 for row in rows if row.get("status") == "ok")
    error_count = sum(1 for row in rows if row.get("status") == "error")
    print(f"Listo. OK: {ok_count}, errores: {error_count}, indice: {index_path}")
    print(f"Tiempo total: {time.time() - started_run:.1f}s")
    return 1 if error_count else 0


if __name__ == "__main__":
    raise SystemExit(main())
