import os
import json
import sys

format = TOOL_ARGS.get("format", "gerber")
output_dir = TOOL_ARGS.get("output_dir", "")

if not output_dir:
    print(json.dumps({'status': 'error', 'message': 'output_dir is required'}))
else:
    # Set KICAD_CLI path if not already set
    if 'KICAD_CLI' not in os.environ:
        # Find kicad-cli relative to the running executable
        exe_dir = os.path.dirname(sys.executable)
        cli_path = os.path.join(exe_dir, 'kicad-cli')
        if not os.path.exists(cli_path):
            # Try macOS app bundle location
            for app_dir in ['/Applications/KiCad/KiCad.app/Contents/MacOS',
                            '/Applications/Zeo/Zeo.app/Contents/MacOS']:
                candidate = os.path.join(app_dir, 'kicad-cli')
                if os.path.exists(candidate):
                    cli_path = candidate
                    break
        if os.path.exists(cli_path):
            os.environ['KICAD_CLI'] = cli_path

    os.makedirs(output_dir, exist_ok=True)

    if format == "gerber":
        try:
            files = board.export.generate_gerbers(output_dir)
            print(json.dumps({'status': 'success', 'format': 'gerber', 'files': files}, indent=2))
        except Exception as e:
            print(json.dumps({'status': 'error', 'message': str(e)}))

    elif format == "drill":
        try:
            files = board.export.generate_drill_files(output_dir)
            print(json.dumps({'status': 'success', 'format': 'drill', 'files': files}, indent=2))
        except Exception as e:
            print(json.dumps({'status': 'error', 'message': str(e)}))

    elif format == "pos":
        try:
            out_path = os.path.join(output_dir, 'positions.csv')
            path = board.export.generate_pos(out_path)
            print(json.dumps({'status': 'success', 'format': 'pos', 'file': path}, indent=2))
        except Exception as e:
            print(json.dumps({'status': 'error', 'message': str(e)}))

    elif format == "step":
        try:
            out_path = os.path.join(output_dir, 'board.step')
            path = board.export.generate_step(out_path)
            print(json.dumps({'status': 'success', 'format': 'step', 'file': path}, indent=2))
        except Exception as e:
            print(json.dumps({'status': 'error', 'message': str(e)}))

    else:
        print(json.dumps({'status': 'error', 'message': f'Unsupported export format: {format}'}))
