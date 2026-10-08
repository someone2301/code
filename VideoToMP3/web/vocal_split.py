"""Split a Demucs vocals stem into lead and background vocals.

Karaoke models are trained on full songs: given a mix, they return the lead
vocal and "everything else". Tested on vocals-only input they wrongly split a
solo voice in two, so this script runs the model on the FULL MIX to get the
lead, then computes background = Demucs vocals - lead.

Run by the web app in a child process, with the Python that has
audio-separator installed (see README):
    python vocal_split.py --model-dir DIR --model NAME MIX.wav VOCALS.wav OUT_DIR
Writes OUT_DIR/lead.wav and OUT_DIR/background.wav (32-bit float, same length
and sample rate as VOCALS.wav).
"""

import argparse
import logging
import sys
import tempfile
from pathlib import Path


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("mix", type=Path)
    ap.add_argument("vocals", type=Path)
    ap.add_argument("output_dir", type=Path)
    ap.add_argument("--model-dir", required=True)
    ap.add_argument("--model", required=True)
    a = ap.parse_args()

    import numpy as np
    import soundfile as sf
    from audio_separator.separator import Separator

    vocals, sr = sf.read(str(a.vocals), dtype="float32", always_2d=True)
    a.output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=a.output_dir) as tmp:
        sep = Separator(output_dir=tmp, model_file_dir=a.model_dir, output_format="WAV",
                        sample_rate=sr, output_single_stem="Vocals",
                        # No peak normalisation: background = vocals - lead needs true gain.
                        normalization_threshold=1.0, amplification_threshold=0.0,
                        log_level=logging.ERROR)
        sep.load_model(model_filename=a.model)
        files = sep.separate(str(a.mix))
        if len(files) != 1:
            sys.exit(f"Expected one lead-vocal output, got {files}")
        out = Path(files[0]) if Path(files[0]).is_absolute() else Path(tmp) / files[0]
        lead, lead_sr = sf.read(str(out), dtype="float32", always_2d=True)
    if lead_sr != sr:
        sys.exit(f"Lead vocal sample rate {lead_sr} differs from vocals stem {sr}")

    n = len(vocals)
    lead = np.pad(lead, ((0, max(0, n - len(lead))), (0, 0)))[:n]
    if lead.shape[1] != vocals.shape[1]:
        lead = np.repeat(lead[:, :1], vocals.shape[1], axis=1)
    background = vocals - lead
    sf.write(str(a.output_dir / "lead.wav"), lead, sr, subtype="FLOAT")
    sf.write(str(a.output_dir / "background.wav"), background, sr, subtype="FLOAT")
    print(f"lead/background written ({n} frames)", file=sys.stderr)


if __name__ == "__main__":
    main()
