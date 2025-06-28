import configparser
from collections import defaultdict
import os
import argparse

def read_build_board_value(ini_file):
    config = configparser.RawConfigParser()
    config.read(ini_file)
    try:
        return config.get('fujinet', 'build_board')
    except (configparser.NoSectionError, configparser.NoOptionError):
        print(f"Error: 'build_board' value not found in the [fujinet] section of {ini_file}.")
        exit(1)

def create_local_ini(board_name, local_file_name):
    config = configparser.RawConfigParser()
    config.add_section('fujinet')
    config.set('fujinet', 'build_board', board_name)
    with open(local_file_name, 'w') as configfile:
        config.write(configfile)
        configfile.write("[env]\n;build_flags += !python3 debug_version.py\n")
    print(f"{local_file_name} file created with build_board = {board_name}")

def merge_ini_files(base_file, local_file, output_file):
    # Use RawConfigParser to avoid interpolation of ${...}
    base_config = configparser.RawConfigParser()
    base_config.read(base_file)

    # Read build_board value from local.ini
    build_board = read_build_board_value(local_file)

    # Construct the filename for the specific build_board.ini file
    build_board_file = f"build-platforms/platformio-{build_board}.ini"
    if not os.path.exists(build_board_file):
        print(f"Error: The file {build_board_file} does not exist.")
        exit(1)

    # Files to merge, in order
    files_to_merge = [base_file, build_board_file, local_file]

    # A dictionary to hold combined values for keys using the "+=" operator
    combined_values = defaultdict(dict)

    for file in files_to_merge:
        additional_config = configparser.RawConfigParser()
        additional_config.read(file)

        for section in additional_config.sections():
            if not base_config.has_section(section):
                base_config.add_section(section)

            for key, value in additional_config.items(section):
                # Check if the key uses the special "+=" syntax
                if key.endswith("+"):
                    real_key = key.rstrip("+").strip()
                    if base_config.has_option(section, real_key):
                        # Trim whitespace and newline characters before concatenating
                        existing_value = base_config.get(section, real_key).strip()
                        new_value = value.strip()
                        combined_value = existing_value + "\n" + new_value if existing_value else new_value
                        combined_values[section][real_key] = combined_value
                    else:
                        combined_values[section][real_key] = value.strip()
                else:
                    base_config.set(section, key, value.strip())

    # Apply combined values from the "+=" operator
    for section, keys in combined_values.items():
        for key, value in keys.items():
            base_config.set(section, key, value)

    # Write the final merged INI content to a file
    with open(output_file, 'w') as merged_file:
        base_config.write(merged_file)

def merge_ini_files_simple(a_file, b_file, output_file):
    base_config = configparser.RawConfigParser()
    base_config.read(a_file)

    # Files to merge, in order
    files_to_merge = [a_file, b_file]

    # A dictionary to hold combined values for keys using the "+=" operator
    combined_values = defaultdict(dict)

    for file in files_to_merge:
        additional_config = configparser.RawConfigParser()
        additional_config.read(file)

        for section in additional_config.sections():
            if not base_config.has_section(section):
                base_config.add_section(section)

            for key, value in additional_config.items(section):
                # Check if the key uses the special "+=" syntax
                if key.endswith("+"):
                    real_key = key.rstrip("+").strip()
                    if base_config.has_option(section, real_key):
                        # Trim whitespace and newline characters before concatenating
                        existing_value = base_config.get(section, real_key).strip()
                        new_value = value.strip()
                        combined_value = existing_value + "\n" + new_value if existing_value else new_value
                        combined_values[section][real_key] = combined_value
                    else:
                        combined_values[section][real_key] = value.strip()
                else:
                    base_config.set(section, key, value.strip())

    # Apply combined values from the "+=" operator
    for section, keys in combined_values.items():
        for key, value in keys.items():
            base_config.set(section, key, value)

    # Write the final merged INI content to a file
    with open(output_file, 'w') as merged_file:
        base_config.write(merged_file)

def main():
    parser = argparse.ArgumentParser(description="platformio.ini generator")
    parser.add_argument("-n", "--new", metavar="board_name", help="Create a new platformio.local.ini file with specified board name before merging.")
    parser.add_argument("-l", "--local-file", metavar="local_file", help="Use specified local_file instead of platformio.local.ini")
    parser.add_argument("-o", "--output-file", metavar="output_file", help="write to output_file instead of default platformio-generated.ini")
    parser.add_argument("-f", "--add-files", metavar="add_files", action="append", help="include additional ini file changes, can be specified multiple times")

    args = parser.parse_args()
    local_file = "platformio.local.ini"
    output_file = "platformio-generated.ini"

    if args.local_file:
        local_file = args.local_file

    if args.new:
        create_local_ini(args.new, local_file)

    if args.output_file:
        output_file = args.output_file

    merge_ini_files('platformio-ini-files/platformio.common.ini', local_file, output_file)

    # merge in files specified
    if args.add_files:
        for f in args.add_files:
            merge_ini_files_simple(output_file, f, output_file)

    print(f"Merged INI file created as '{output_file}'.")

if __name__ == "__main__":
    main()
