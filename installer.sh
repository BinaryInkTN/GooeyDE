#!/bin/bash


set -u

LOG_FILE="/tmp/gooey_install_$(date +%s).log"
WORK_DIR="${HOME}/.gooey_build"
GOOEYGUI_REPO="https://github.com/BinaryInkTN/GooeyGUI.git"
GOOEYDE_REPO="https://github.com/BinaryInkTN/GooeyDE.git"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
PURPLE='\033[0;35m'
NC='\033[0m' # No Color

PREREQUISITES=(
    "git"
    "build-essential"
    "cmake"
    "libdrm-dev"
    "libgbm-dev"
    "libegl-dev"
    "libgl-dev"
    "libwayland-dev"
    "libxkbcommon-dev"
    "mesa-utils"
    "sudo"
)


show_logo() {
    clear
    echo -e "${CYAN}"
    cat << "EOF"
  /$$$$$$                                          /$$$$$$$  /$$$$$$$$
 /$$__  $$                                        | $$__  $$| $$_____/
| $$  \__/  /$$$$$$   /$$$$$$   /$$$$$$  /$$   /$$| $$  \ $$| $$      
| $$ /$$$$ /$$__  $$ /$$__  $$ /$$__  $$| $$  | $$| $$  | $$| $$$$$   
| $$|_  $$| $$  \ $$| $$  \ $$| $$$$$$$$| $$  | $$| $$  | $$| $$__/   
| $$  \ $$| $$  | $$| $$  | $$| $$_____/| $$  | $$| $$  | $$| $$      
|  $$$$$$/|  $$$$$$/|  $$$$$$/|  $$$$$$$|  $$$$$$$| $$$$$$$/| $$$$$$$$
 \______/  \______/  \______/  \_______/ \____  $$|_______/ |________/
                                         /$$  | $$                    
                                        |  $$$$$$/                    
                                         \______/                     
EOF
    echo -e "${NC}"
    echo -e "${PURPLE}           Desktop Environment Installation Script${NC}"
    echo -e "${YELLOW}==============================================================${NC}"
    echo -e "Logs are being saved to: ${LOG_FILE}"
    echo ""
}

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

check_sudo_access() {
    log_info "Checking sudo access..."
    if ! sudo -n true 2>/dev/null; then
        echo -e "${YELLOW}"
        echo "=============================================================="
        echo "Sudo access required for installation"
        echo "Please enter your password when prompted"
        echo "=============================================================="
        echo -e "${NC}"
        
        if ! sudo -v; then
            log_error "Failed to get sudo access. Exiting."
            exit 1
        fi
    fi
    log_success "Sudo access confirmed"
}

run_with_spinner() {
    local message="$1"
    local command="$2"
    local pid
    local delay=0.1
    local spinstr='|/-\'
    
    eval "$command" >> "$LOG_FILE" 2>&1 &
    pid=$!

    echo -ne "${BLUE}[PROCESSING]${NC} ${message}... "

    while kill -0 "$pid" 2>/dev/null; do
        local temp=${spinstr#?}
        printf " [%c]  " "$spinstr"
        local spinstr=$temp${spinstr%"$temp"}
        sleep $delay
        printf "\b\b\b\b\b\b"
    done

    wait "$pid"
    local exit_code=$?

    if [ $exit_code -eq 0 ]; then
        printf "      \b\b\b\b\b\b${GREEN}[DONE]${NC}   \n"
        return 0
    else
        printf "      \b\b\b\b\b\b${RED}[FAIL]${NC}   \n"
        log_error "Command failed. See details below:"
        echo "----------------------------------------"
        tail -n 20 "$LOG_FILE"
        echo "----------------------------------------"
        log_error "Full log available at: $LOG_FILE"
        return 1
    fi
}

run_sudo_with_prompt() {
    local message="$1"
    local command="$2"
    
    echo -e "${YELLOW}"
    echo "=============================================================="
    echo "Sudo required for: $message"
    echo "Please enter your password if prompted"
    echo "=============================================================="
    echo -e "${NC}"
    
    run_with_spinner "$message" "$command"
}

check_distro() {
    if [ ! -f /etc/debian_version ]; then
        log_warn "This script is designed for Debian/Ubuntu based systems (uses apt)."
        read -p "Are you sure you want to continue? (y/n) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
}

cleanup() {
    echo ""
}
trap cleanup EXIT


if [[ $EUID -eq 0 ]]; then
   log_error "Please run this script as a regular user (not root)."
   exit 1
fi

check_distro
show_logo

check_sudo_access

if [ ! -d "$WORK_DIR" ]; then
    mkdir -p "$WORK_DIR"
fi

echo -e "${PURPLE}==> Step 1: Installing Dependencies${NC}"

run_sudo_with_prompt "Updating package lists" "sudo apt update" || exit 1

PKG_LIST="${PREREQUISITES[*]}"
run_sudo_with_prompt "Installing required packages" "sudo apt install -y $PKG_LIST" || exit 1
log_success "Dependencies installed."
echo ""

build_and_install() {
    local name="$1"
    local repo="$2"
    local dir_name="$3"
    local custom_install_cmd="${4:-}" 

    echo -e "${PURPLE}==> Step: Installing $name${NC}"
    
    cd "$WORK_DIR" || exit 1

    if [ -d "$dir_name" ]; then
        log_info "$name directory exists. Updating..."
        cd "$dir_name" || exit 1
        run_with_spinner "Pulling latest changes" "git pull" || exit 1
    else
        log_info "Cloning $name..."
        run_with_spinner "Cloning repository" "git clone '$repo' '$dir_name'" || exit 1
        cd "$dir_name" || exit 1
    fi

    if [ -f ".gitmodules" ]; then
        run_with_spinner "Updating submodules" "git submodule update --init --recursive" || exit 1
    fi

    if [ -d "build" ]; then
        rm -rf build
    fi
    
    run_with_spinner "Configuring with CMake" "cmake -S . -B build" || exit 1

    local cores=$(nproc)
    run_with_spinner "Compiling $name (using $cores cores)" "make -C build -j$cores" || exit 1

    if [ -n "$custom_install_cmd" ]; then
        chmod +x "$custom_install_cmd"
        run_sudo_with_prompt "Running custom install script" "sudo ./$custom_install_cmd" || exit 1
    else
        run_sudo_with_prompt "Installing to system" "sudo make -C build install" || exit 1
    fi

    log_success "$name installed successfully!"
    echo ""
}

build_and_install "GooeyGUI" "$GOOEYGUI_REPO" "GooeyGUI"

build_and_install "GooeyDE" "$GOOEYDE_REPO" "GooeyDE" "install_on_sys"

show_logo
log_success "Installation Complete!"
echo -e "${CYAN}--------------------------------------------------------------${NC}"
echo -e "Next Steps:"
echo -e "  1. Log out of your current session."
echo -e "  2. At the login screen, click the gear/session icon."
echo -e "  3. Select 'gooey_shell'."
echo -e "  4. Log in."
echo -e "${CYAN}--------------------------------------------------------------${NC}"

sudo -k 