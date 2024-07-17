import csv
import pathlib

production = pathlib.Path(__file__).parent / "production"

positions = csv.reader((production / "positions.csv").open())
connectors = []

for row in positions:
    reference, x, y, rotation, layer = row
    if not reference.startswith("Module"):
        continue
    x = float(x)
    y = float(y)
    connectors.append((f"X{len(connectors) + 1}", x + 0.46, y - 21.5, rotation, layer))
    connectors.append((f"X{len(connectors) + 1}", x - 33.46, y - 21.5, rotation, layer))

(production / "positions.csv").open(mode="a").write(
    "\n".join(",".join(map(str, c)) for c in connectors) + "\n"
)
(production / "bom.csv").open(mode="a").write(
    '"{}",10164227-1001A1RLF,{},10164227-1001A1RLF,C6782225'.format(
        ", ".join(r[0] for r in connectors), len(connectors)
    )
)
