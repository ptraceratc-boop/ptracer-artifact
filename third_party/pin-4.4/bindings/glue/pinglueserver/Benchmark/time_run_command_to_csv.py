#
# Copyright (C) 2024-2024 Intel Corporation.
# SPDX-License-Identifier: MIT
#

import subprocess
import time
import argparse
import csv

def run_command(command, iterations):
    times = []
    for i in range(iterations):
        start_time = time.time()
        subprocess.run(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        end_time = time.time()
        elapsed_time = end_time - start_time
        times.append(elapsed_time)
        print(f"Run {i + 1}: {elapsed_time:.6f} seconds")

    return times

def update_csv(existing_csv, new_results, new_column_name="NewColumn"):
    try:
        with open(existing_csv, 'r', newline='') as file:
            csv_reader = csv.reader(file)
            data = list(csv_reader)

        # Add a new column header
        data[0].append(new_column_name)

        # Add new results to the new column
        for i, result in enumerate(new_results):
            data[i + 1].append(result)

        with open(existing_csv, 'w', newline='') as file:
            csv_writer = csv.writer(file)
            csv_writer.writerows(data)

        print(f"Results added to {existing_csv}")
    except FileNotFoundError:
        print(f"Creating a new file: {existing_csv}")
        header = ["Run", new_column_name]
        data = list(enumerate(new_results, start=1))
        write_to_csv(existing_csv, header, data)

def write_to_csv(file_path, header, data):
    with open(file_path, 'w', newline='') as file:
        csv_writer = csv.writer(file)
        
        # Write header
        csv_writer.writerow(header)

        # Write data
        csv_writer.writerows(data)

    print(f"Results saved to {file_path}")

def main():
    parser = argparse.ArgumentParser(description="Run a command multiple times and record the execution time.")
    parser.add_argument("command", help="Command to run")
    parser.add_argument("iterations", type=int, help="Number of iterations to run the command")
    parser.add_argument("--csv", help="Path to the csv file (optional)")
    parser.add_argument("--name", default="NewColumn", help="Name of the new column (default: NewColumn)")
    args = parser.parse_args()

    command = args.command
    iterations = args.iterations

    if args.csv:
        existing_csv = args.csv
        new_results = run_command(command, iterations)
        update_csv(existing_csv, new_results, new_column_name=args.name)
    else:
        csv_file = "results.csv"
        new_results = run_command(command, iterations)

        # Writing results to csv (CSV) file
        header = ["Run", "Execution Time"]
        data = list(enumerate(new_results, start=1))

        write_to_csv(csv_file, header, data)

if __name__ == "__main__":
    main()
