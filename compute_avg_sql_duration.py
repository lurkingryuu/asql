import os
import glob
import pandas as pd
from collections import defaultdict

# Set this to the directory where your .tsv files are located
DATA_DIR = "./profile_out"  # update if different

# Dictionary to collect durations per command
durations_by_sql_text = defaultdict(list)

# Iterate over all .tsv files
for file_path in glob.glob(os.path.join(DATA_DIR, "*.tsv")):
    df = pd.read_csv(file_path, sep="\t")

    for _, row in df.iterrows():
        sql_text = row["SQL_TEXT"].strip()
        duration = float(row["Duration_in_ns"])
        
        durations_by_sql_text[sql_text].append(duration)

# Calculate and print average durations
print("Average Duration (in ns) per SQL Command:\n")
for sql_text, durations in durations_by_sql_text.items():
    avg_duration = sum(durations) / len(durations)
    print(f"{avg_duration:3.6f} ns [{len(durations)}]: {sql_text}")
