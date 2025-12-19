#!/bin/bash

# Script to remove Sidekick references from monerujo.cpp
# This script will create a backup and modify the file in place

set -e

# Color codes
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Files to modify (relative path from script location)
CPP_FILE="app/src/main/cpp/monerujo.cpp"
HEADER_FILE="app/src/main/cpp/monerujo.h"

# Check if files exist
if [ ! -f "$CPP_FILE" ]; then
    echo -e "${RED}Error: $CPP_FILE not found${NC}"
    echo "Expected location: $CPP_FILE"
    echo "Current directory: $(pwd)"
    echo ""
    echo "Please run this script from the Monerujo project root directory"
    exit 1
fi

if [ ! -f "$HEADER_FILE" ]; then
    echo -e "${YELLOW}Warning: $HEADER_FILE not found${NC}"
    echo "Will only process the .cpp file"
    HEADER_FILE=""
fi

echo -e "${BLUE}================================================${NC}"
echo -e "${BLUE}Remove Sidekick References from monerujo.cpp${NC}"
echo -e "${BLUE}================================================${NC}"
echo ""

# Create backup
BACKUP_FILE="${CPP_FILE}.backup.$(date +%Y%m%d_%H%M%S)"
echo -e "${YELLOW}Creating backup: $BACKUP_FILE${NC}"
cp "$CPP_FILE" "$BACKUP_FILE"

# Temporary file for modifications
TEMP_FILE="${CPP_FILE}.tmp"
TEMP_HEADER="${HEADER_FILE}.tmp"

echo -e "${GREEN}Processing files...${NC}"
echo ""

# Process header file first if it exists
if [ -n "$HEADER_FILE" ]; then
    echo -e "${BLUE}Checking $HEADER_FILE for Sidekick references...${NC}"
    HEADER_BACKUP="${HEADER_FILE}.backup.$(date +%Y%m%d_%H%M%S)"
    cp "$HEADER_FILE" "$HEADER_BACKUP"
    
    # Check if header has ConfirmTransfers declaration
    if grep -q "bool ConfirmTransfers" "$HEADER_FILE"; then
        echo -e "${YELLOW}Found ConfirmTransfers declaration in header${NC}"
        # Remove or comment out ConfirmTransfers declaration
        sed '/bool ConfirmTransfers/d' "$HEADER_FILE" > "$TEMP_HEADER"
        mv "$TEMP_HEADER" "$HEADER_FILE"
        echo -e "${GREEN}✓ Removed ConfirmTransfers from header${NC}"
    else
        echo -e "${GREEN}✓ No Sidekick references found in header${NC}"
    fi
    echo ""
fi

echo -e "${BLUE}Processing $CPP_FILE...${NC}"

# Use awk to process the file
awk '
BEGIN {
    in_confirm_transfers = 0
    skip_sidekick_class = 0
    changes_made = 0
}

# Skip the global class_SidekickService declaration (around line 27)
/^jclass class_SidekickService;/ {
    print "// REMOVED: jclass class_SidekickService; (Sidekick disabled)"
    changes_made++
    next
}

# Skip the SidekickService class loading in JNI_OnLoad (around lines 237-239)
/class_SidekickService = static_cast<jclass>\(jenv->NewGlobalRef\(/ {
    skip_sidekick_class = 1
    print "    // REMOVED: class_SidekickService initialization (Sidekick disabled)"
    changes_made++
    next
}

# Continue skipping if we are in the middle of the class_SidekickService initialization
skip_sidekick_class == 1 {
    if (/jenv->FindClass\("com\/m2049r\/xmrwallet\/service\/SidekickService"\)\)\);/) {
        skip_sidekick_class = 0
        next
    }
    next
}

# Detect start of ConfirmTransfers function
/^bool ConfirmTransfers\(const char \*transfers\) {/ {
    in_confirm_transfers = 1
    print "// MODIFIED: ConfirmTransfers function (Sidekick disabled)"
    print "bool ConfirmTransfers(const char *transfers) {"
    print "    // Sidekick functionality disabled - auto-confirm transfers"
    print "    LOGD(\"ConfirmTransfers: auto-confirming (Sidekick disabled)\");"
    print "    return true;"
    print "}"
    changes_made++
    next
}

# Skip lines inside ConfirmTransfers function
in_confirm_transfers == 1 {
    if (/^}/) {
        in_confirm_transfers = 0
    }
    next
}

# Print all other lines unchanged
{
    print
}

END {
    print "" > "/dev/stderr"
    if (changes_made > 0) {
        print "✓ Successfully removed " changes_made " Sidekick reference(s)" > "/dev/stderr"
    } else {
        print "⚠ Warning: No Sidekick references found to remove" > "/dev/stderr"
    }
}
' "$CPP_FILE" > "$TEMP_FILE"

# Check if awk succeeded
if [ $? -eq 0 ]; then
    mv "$TEMP_FILE" "$CPP_FILE"
    echo ""
    echo -e "${GREEN}✓ Successfully modified $CPP_FILE${NC}"
    echo -e "${GREEN}✓ Backup saved as: $BACKUP_FILE${NC}"
    if [ -n "$HEADER_FILE" ]; then
        echo -e "${GREEN}✓ Header backup saved as: $HEADER_BACKUP${NC}"
    fi
    echo ""
    
    # Show what was changed
    echo -e "${BLUE}Changes made to $CPP_FILE:${NC}"
    echo "  1. Removed global class_SidekickService declaration"
    echo "  2. Removed class_SidekickService initialization in JNI_OnLoad"
    echo "  3. Replaced ConfirmTransfers function with auto-confirm stub"
    if [ -n "$HEADER_FILE" ] && [ -f "$HEADER_BACKUP" ]; then
        if ! diff -q "$HEADER_BACKUP" "$HEADER_FILE" > /dev/null 2>&1; then
            echo ""
            echo -e "${BLUE}Changes made to $HEADER_FILE:${NC}"
            echo "  1. Removed ConfirmTransfers function declaration"
        fi
    fi
    echo ""
    
    # Show diff summary
    echo -e "${BLUE}Diff summary:${NC}"
    diff -u "$BACKUP_FILE" "$CPP_FILE" | grep -E "^[\+\-]" | head -20 || true
    echo ""
    
    echo -e "${GREEN}================================================${NC}"
    echo -e "${GREEN}Next steps:${NC}"
    echo -e "${GREEN}================================================${NC}"
    echo "1. Review the changes in $CPP_FILE"
    echo "2. Rebuild the native libraries:"
    echo "   cd external-libs && ./build.sh"
    echo "   ndk-build"
    echo "3. Copy the new .so files to your project"
    echo ""
    echo -e "To restore the original file: ${YELLOW}mv $BACKUP_FILE $CPP_FILE${NC}"
else
    echo -e "${RED}Error: Failed to modify file${NC}"
    rm -f "$TEMP_FILE"
    exit 1
fi
