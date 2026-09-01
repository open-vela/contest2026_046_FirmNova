#!/usr/bin/env python3
"""Manual Codex session log collector.
Usage: python3 collect_codex_log.py [session_jsonl_path]
If no path given, collects the latest session.
"""
import sys, os, json
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent

os.environ['TEAM_ID'] = os.environ.get('TEAM_ID', 'contest2026_046_FirmNova')
os.environ['GITHUB_LOGIN'] = os.environ.get('GITHUB_LOGIN', 'zhoux-in')
os.environ['SESSION_LOG_DIR'] = os.environ.get('SESSION_LOG_DIR', str(SCRIPT_DIR / 'logs'))

# Import snapshot_core_patched from the same directory (Codex-aware version)
sys.path.insert(0, str(SCRIPT_DIR))
import snapshot_core_patched as sc

def find_latest_session():
    sessions_dir = Path.home() / '.codex' / 'sessions'
    files = sorted(sessions_dir.rglob('rollout-*.jsonl'), key=lambda p: p.stat().st_mtime)
    return files[-1] if files else None

def main():
    if len(sys.argv) > 1:
        transcript = Path(sys.argv[1])
    else:
        transcript = find_latest_session()

    if not transcript or not transcript.exists():
        print("ERROR: No session transcript found", file=sys.stderr)
        return 1

    session_id = transcript.stem.replace('rollout-', '')
    stdin_data = {
        'session_id': session_id,
        'cwd': str(Path.cwd()),
        'transcript_path': str(transcript),
        'hook_event_name': 'Stop',
    }

    print(f"Collecting: {transcript}", file=sys.stderr)
    print(f"Session ID: {session_id}", file=sys.stderr)

    result = sc.process_claude_stdin(stdin_data, 'codex', os.environ['TEAM_ID'])
    return result

if __name__ == '__main__':
    sys.exit(main())
