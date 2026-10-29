"""Record the derived host layer's tested GUI control protocol and exact binary."""
import hashlib
import json
from pathlib import Path
import sys
layer=Path(sys.argv[1])
(layer.parent/'capture-control.json').write_text(json.dumps(dict(gui_capture_protocol=1,
    base_revision='a20d3dbf83df4f8e02d52e42f19f0ea060708237',
    layer_sha256=hashlib.sha256(layer.read_bytes()).hexdigest()),indent=2)+'\n',encoding='utf-8')
