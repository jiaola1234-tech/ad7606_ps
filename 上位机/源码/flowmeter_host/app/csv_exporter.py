from pathlib import Path

import pandas as pd

from .data_buffer import BufferSnapshot
from .settings import CHANNEL_COUNT


def export_snapshot(snapshot: BufferSnapshot, path: str | Path) -> None:
    columns = {"timestamp": snapshot.timestamps}
    for idx in range(CHANNEL_COUNT):
        columns[f"ch{idx + 1}"] = snapshot.channels[:, idx] if snapshot.channels.size else []
    columns["flow_velocity"] = snapshot.flow_velocity
    df = pd.DataFrame(columns)
    df.to_csv(path, index=False, encoding="utf-8-sig")
