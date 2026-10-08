"""Split a drum stem into kick, snare, toms, hi-hat and cymbals with LarsNet.

LarsNet (https://github.com/polimi-ispl/larsnet) is not bundled; see README.
Its pretrained weights are licensed CC BY-NC 4.0 (non-commercial use only).

Run by the web app in a child process:
    python drum_split.py --larsnet-dir /opt/larsnet DRUMS.wav OUT_DIR

One-time setup after downloading the weights:
    python drum_split.py --larsnet-dir /opt/larsnet --prepare-weights
"""

import argparse
import contextlib
import functools
import io
import os
import sys
from pathlib import Path


def load_larsnet(larsnet_dir: Path, device: str):
    sys.path.insert(0, str(larsnet_dir))
    os.chdir(larsnet_dir)  # config.yaml uses paths relative to the LarsNet folder
    import torch
    # Only load plain tensors at runtime; --prepare-weights converts the checkpoints.
    torch.load = functools.partial(torch.load, weights_only=True)
    from larsnet import LarsNet
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return LarsNet(wiener_filter=False, device=device, config="config.yaml")


def prepare_weights(larsnet_dir: Path) -> None:
    """Re-save each checkpoint as tensors only, so it loads with weights_only=True."""
    import torch
    import yaml
    config = yaml.safe_load((larsnet_dir / "config.yaml").read_text())
    for stem, rel in config["inference_models"].items():
        path = larsnet_dir / rel
        try:
            ckpt = torch.load(path, map_location="cpu", weights_only=True)
            print(f"{stem}: already safe")
        except Exception:  # noqa: BLE001
            # Full unpickling runs only here, on a file you downloaded and chose to trust.
            ckpt = torch.load(path, map_location="cpu", weights_only=False)
            print(f"{stem}: converted")
        torch.save({"model_state_dict": ckpt["model_state_dict"]}, path)


def separate(larsnet_dir: Path, inp: Path, out: Path, device: str, batch: int) -> None:
    import soundfile as sf
    import torch

    net = load_larsnet(larsnet_dir, device)
    audio, sr = sf.read(str(inp), dtype="float32", always_2d=True)
    if sr != net.sr:
        raise SystemExit(f"Expected {net.sr} Hz input, got {sr} Hz")
    x = torch.from_numpy(audio.T.copy())
    if x.size(0) == 1:
        x = x.repeat(2, 1)
    x = x[:2].unsqueeze(0).to(device)  # (1, 2, samples)

    out.mkdir(parents=True, exist_ok=True)
    stems = list(net.models.items())
    with torch.no_grad():
        for i, (stem, model) in enumerate(stems):
            # Same steps as LarsNet's UNetWaveform.forward, but the ~12 s chunks go
            # through the network a few at a time instead of all at once, which keeps
            # memory use flat for long songs. Chunks are independent, so output is identical.
            u = model.utils
            mag, phase = u.batch_stft(x)
            folded = u.fold_unet_inputs(mag)
            masks = []
            for j in range(0, folded.size(0), batch):
                part = u.trim_freq_dim(folded[j:j + batch])
                masks.append(u.pad_freq_dim(model.produce_mask(part)))
                done = min(j + batch, folded.size(0)) / folded.size(0)
                print(f"\r{int(100 * (i + done) / len(stems))}%|", end="", file=sys.stderr, flush=True)
            mag_hat = u.unfold_unet_outputs(folded * torch.cat(masks), mag.size())
            y = u.batch_istft(mag_hat, phase, trim_length=x.size(-1)).squeeze(0)
            sf.write(str(out / f"{stem}.wav"), y.cpu().numpy().T, net.sr, subtype="FLOAT")
    print(file=sys.stderr)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("input", nargs="?")
    ap.add_argument("output_dir", nargs="?")
    ap.add_argument("--larsnet-dir", required=True, type=Path)
    ap.add_argument("--device", default="cpu")
    ap.add_argument("--batch", type=int, default=4, help="chunks per pass (memory vs speed)")
    ap.add_argument("--prepare-weights", action="store_true")
    a = ap.parse_args()
    larsnet_dir = a.larsnet_dir.resolve()
    if a.prepare_weights:
        prepare_weights(larsnet_dir)
        return
    if not a.input or not a.output_dir:
        ap.error("input and output_dir are required")
    separate(larsnet_dir, Path(a.input).resolve(), Path(a.output_dir).resolve(), a.device, a.batch)


if __name__ == "__main__":
    main()
