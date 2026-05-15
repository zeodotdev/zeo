import json

command = TOOL_ARGS.get("command", "")
save_to_schematic = TOOL_ARGS.get("save_to_schematic", False)
plot_signals = TOOL_ARGS.get("plot_signals", None)  # Optional list of signal names to plot

if not command:
    print('Error: command parameter is required')
else:
    # Optionally persist the command to schematic settings
    if save_to_schematic:
        try:
            sch.simulation.set_settings(spice_command=command)
        except Exception as e:
            print(f'Warning: Could not save settings: {e}')

    # Run SPICE simulation
    try:
        result = sch.simulation.run(command_override=command)
        if result['success']:
            # Summarize traces (min/max/final/count)
            trace_summaries = []
            for trace in result.get('traces', []):
                vals = trace.get('data_values', [])
                summary = {'name': trace['name'], 'points': len(vals)}
                if vals:
                    summary['min'] = min(vals)
                    summary['max'] = max(vals)
                    summary['final'] = vals[-1]
                trace_summaries.append(summary)
            summary_output = {
                'status': 'success',
                'command': command,
                'trace_count': len(result.get('traces', [])),
                'traces': trace_summaries
            }

            # Generate plot if traces have array data (skip for .op)
            traces = result.get('traces', [])
            has_arrays = any(len(t.get('data_values', [])) > 1 for t in traces)
            plot_b64 = None
            display_b64 = None
            if has_arrays:
                try:
                    import matplotlib
                    matplotlib.use('Agg')
                    import matplotlib.pyplot as plt
                    import base64
                    from io import BytesIO

                    fig, ax = plt.subplots(figsize=(7, 4))
                    x_data = traces[0].get('time_values', [])
                    cmd_lower = command.lower().strip()
                    if cmd_lower.startswith('.ac'):
                        ax.set_xlabel('Frequency (Hz)')
                        ax.set_ylabel('Magnitude')
                        ax.set_xscale('log')
                    elif cmd_lower.startswith('.dc'):
                        ax.set_xlabel('Sweep Variable')
                        ax.set_ylabel('Voltage / Current')
                    elif cmd_lower.startswith('.tran'):
                        ax.set_xlabel('Time (s)')
                        ax.set_ylabel('Voltage / Current')
                    else:
                        ax.set_xlabel('X')
                        ax.set_ylabel('Y')
                    # Select which traces to plot
                    plot_traces = []
                    if plot_signals:
                        # User/agent specified exact signals to plot
                        signal_set = set(s.lower() for s in plot_signals)
                        for trace in traces:
                            name = trace.get('name', '')
                            y_data = trace.get('data_values', [])
                            if len(y_data) <= 1:
                                continue
                            if name.lower() in signal_set:
                                plot_traces.append(trace)
                    else:
                        # Default: auto-filter to user-facing signals
                        # Keep net labels (/in, /out), named nodes (net-_Q1-B_)
                        # Skip internal subcircuit nodes (contain '.'),
                        # branch currents (*#branch), constant rails
                        for trace in traces:
                            name = trace.get('name', '')
                            y_data = trace.get('data_values', [])
                            if len(y_data) <= 1:
                                continue
                            if '#branch' in name:
                                continue
                            if '.' in name:
                                continue
                            if min(y_data) == max(y_data):
                                continue
                            plot_traces.append(trace)

                    # Fall back to all non-constant traces if filtering
                    # removed everything
                    if not plot_traces:
                        for trace in traces:
                            y_data = trace.get('data_values', [])
                            if len(y_data) > 1 and min(y_data) != max(y_data):
                                plot_traces.append(trace)

                    for trace in plot_traces:
                        name = trace.get('name', '')
                        y_data = trace.get('data_values', [])
                        t_data = trace.get('time_values', x_data)
                        if len(t_data) == len(y_data):
                            ax.plot(t_data, y_data, label=name, linewidth=1.5)
                    ax.legend(fontsize=8, loc='best')
                    ax.grid(True, alpha=0.3)
                    ax.set_title(command, fontsize=10)
                    fig.tight_layout()
                    # High-res for chat UI display
                    buf_display = BytesIO()
                    fig.savefig(buf_display, format='png', bbox_inches='tight', dpi=650)
                    buf_display.seek(0)
                    display_b64 = base64.b64encode(buf_display.read()).decode('ascii')
                    # Low-res for LLM API (fits Claude vision limits)
                    buf_api = BytesIO()
                    fig.savefig(buf_api, format='png', bbox_inches='tight', dpi=200)
                    buf_api.seek(0)
                    plot_b64 = base64.b64encode(buf_api.read()).decode('ascii')
                    plt.close(fig)
                except ImportError as _ie:
                    summary_output['_plot_error'] = 'ImportError: ' + str(_ie)
                except Exception as _pe:
                    summary_output['_plot_error'] = 'PlotError: ' + str(_pe)

            if plot_b64:
                output = {
                    'text': json.dumps(summary_output, indent=2),
                    'image': {
                        'media_type': 'image/png',
                        'base64': plot_b64
                    },
                    'display_image': {
                        'media_type': 'image/png',
                        'base64': display_b64
                    }
                }
            else:
                output = summary_output

            print(json.dumps(output))
        else:
            print(json.dumps({
                'status': 'error',
                'message': result.get('error_message', 'Simulation failed')
            }))
    except Exception as e:
        print(json.dumps({'status': 'error', 'message': str(e)}))
