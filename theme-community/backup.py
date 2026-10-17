from pathlib import Path
import argparse,sqlite3
p=argparse.ArgumentParser();p.add_argument('--data',type=Path,default=Path(__file__).parent/'data');p.add_argument('--output',type=Path,required=True);a=p.parse_args()
if a.output.exists():p.error('Output already exists; choose a new backup name')
src=sqlite3.connect(f'file:{(a.data/"community.sqlite3").as_posix()}?mode=ro',uri=True);dst=sqlite3.connect(a.output)
try:
 with dst:src.backup(dst)
finally:dst.close();src.close()
print('Backup complete')
