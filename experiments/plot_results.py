import argparse
from pathlib import Path

import pandas as pd
import matplotlib.pyplot as plt


def save_line(df, x, y, title, xlabel, ylabel, out_path, group=None):
    plt.figure(figsize=(8, 5))
    if group:
        for key, part in df.groupby(group):
            part = part.sort_values(x)
            plt.plot(part[x], part[y], marker='o', label=f'{group}={key}')
        plt.legend()
    else:
        df = df.sort_values(x)
        plt.plot(df[x], df[y], marker='o')
    plt.title(title)
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.grid(True, alpha=0.35)
    plt.tight_layout()
    plt.savefig(out_path, dpi=160)
    plt.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--input', default='results/benchmark.csv')
    parser.add_argument('--out', default='results/graphs')
    args = parser.parse_args()

    df = pd.read_csv(args.input)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    largest_batch = df['batch_size'].max()
    thread_df = df[df['batch_size'] == largest_batch]
    smallest_thread = df['thread_count'].min()
    batch_df = df[df['thread_count'] == smallest_thread]

    save_line(
        thread_df,
        'thread_count',
        'throughput_images_per_sec',
        'Throughput vs. OpenMP Thread Count',
        'Thread count',
        'Throughput (images/sec)',
        out / 'throughput_vs_threads.png',
    )

    save_line(
        thread_df,
        'thread_count',
        'speedup',
        'Speedup vs. OpenMP Thread Count',
        'Thread count',
        'Speedup over baseline',
        out / 'speedup_vs_threads.png',
    )

    save_line(
        batch_df,
        'batch_size',
        'avg_image_latency_ms',
        'Average Image Latency vs. Batch Size',
        'Batch size',
        'Avg image latency (ms)',
        out / 'latency_vs_batch_size.png',
    )

    save_line(
        df,
        'batch_size',
        'grpc_overhead_ms',
        'Estimated gRPC/Serialization Overhead vs. Batch Size',
        'Batch size',
        'E2E latency - server wall time (ms)',
        out / 'grpc_overhead_vs_batch_size.png',
        group='thread_count',
    )

    summary = df.groupby('thread_count', as_index=False)[['e2e_latency_ms', 'server_wall_time_ms', 'grpc_overhead_ms']].mean()
    summary.plot(x='thread_count', kind='bar', figsize=(8, 5))
    plt.title('Average Latency Breakdown by Thread Count')
    plt.xlabel('Thread count')
    plt.ylabel('Milliseconds')
    plt.tight_layout()
    plt.savefig(out / 'processing_breakdown.png', dpi=160)
    plt.close()

    print(f'Wrote graphs to {out}')


if __name__ == '__main__':
    main()
