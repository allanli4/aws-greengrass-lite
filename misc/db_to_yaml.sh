#!/bin/bash

# Global variables
DB_PATH=""
OUTPUT_FILE=""

# Show help message
show_help() {
    echo "Usage: $0 <path_to_config.db>"
    echo "Convert Greengrass configuration database to YAML format"
    echo ""
    echo "Arguments:"
    echo "  <path_to_config.db>  Path to the configuration database file"
    echo ""
    echo "Options:"
    echo "  -h, --help           Show this help message and exit"
}

# Validate input arguments
validate_args() {
    if [ "$1" = "--help" ] || [ "$1" = "-h" ]; then
        show_help
        exit 0
    fi

    if [ $# -ne 1 ]; then
        echo "Usage: $0 <path_to_config.db>"
        exit 1
    fi

    if [ ! -f "$1" ]; then
        echo "Error: Database file $1 not found"
        exit 1
    fi
}

# Get child count for a given key ID
get_child_count() {
    local key_id="$1"
    sqlite3 "$DB_PATH" "SELECT COUNT(*) FROM relationTable WHERE parentid = $key_id;"
}

# Get nodes for a given parent ID
get_nodes() {
    local parent_id="$1"
    local query="SELECT k.keyvalue, k.keyid, v.value FROM keyTable k
                 LEFT JOIN relationTable r ON k.keyid = r.keyid
                 LEFT JOIN valueTable v ON k.keyid = v.keyid
                 WHERE r.parentid $parent_id ORDER BY k.keyid;"
    sqlite3 "$DB_PATH" "$query"
}

# Format value based on its type
format_value() {
    local value="$1"
    local indent="$2"
    local key="$3"
    local clean_value

    clean_value=$(echo "$value" | sed 's/^"//; s/"$//')

    # Check if it's a JSON array
    if [[ "$clean_value" =~ ^\[.*\]$ ]]; then
        echo "${indent}${key}:" >> "$OUTPUT_FILE"
        echo "$clean_value" | sed 's/\[//; s/\]//; s/","/"\n/g; s/"//g' | while read -r item; do
            [ -n "$item" ] && echo "${indent}  - \"$item\"" >> "$OUTPUT_FILE"
        done
    # Check if it's a number
    elif [[ "$clean_value" =~ ^[0-9]+$ ]]; then
        echo "${indent}${key}: $clean_value" >> "$OUTPUT_FILE"
    # Otherwise, treat as string
    else
        echo "${indent}${key}: \"$clean_value\"" >> "$OUTPUT_FILE"
    fi
}

# Process database nodes recursively
process_nodes() {
    local parent_id="$1"
    local indent="$2"

    while IFS='|' read -r key_name key_id value; do
        local child_count
        child_count=$(get_child_count "$key_id")

        if [ "$child_count" -gt 0 ]; then
            # Has children - create section
            echo "${indent}${key_name}:" >> "$OUTPUT_FILE"
            process_nodes "= $key_id" "$indent  "
        elif [ -n "$value" ]; then
            # Has value - format and write
            format_value "$value" "$indent" "$key_name"
        else
            # Empty object
            echo "${indent}${key_name}: {}" >> "$OUTPUT_FILE"
        fi
    done < <(get_nodes "$parent_id")
}

# Main function
main() {
    validate_args "$@"

    DB_PATH="$1"
    OUTPUT_FILE=$(mktemp)

    # Start YAML output
    echo "---" > "$OUTPUT_FILE"

    # Process root nodes (where parentid IS NULL)
    process_nodes "IS NULL" ""

    # Output result and cleanup
    cat "$OUTPUT_FILE"
    rm "$OUTPUT_FILE"
}

# Run main function with all arguments
main "$@"
