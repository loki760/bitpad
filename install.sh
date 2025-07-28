#!/bin/bash

set -e

REPO="loki760/bitpad"  
VERSION="v1.1.1"                 
NAME="bitpad"                      

# Detect OS
OS=$(uname -s)
BINARY_URL=""

case "$OS" in
  Linux*)
    BINARY_URL="https://github.com/$REPO/releases/download/$VERSION/${NAME}-linux"
    ;;
  Darwin*)
    BINARY_URL="https://github.com/$REPO/releases/download/$VERSION/${NAME}-macos"
    ;;
  *)
    echo "Unsupported OS: $OS"
    exit 1
    ;;
esac

# Download and install
echo "⬇Downloading $NAME binary for $OS..."
curl -L "$BINARY_URL" -o "/tmp/$NAME"
chmod +x "/tmp/$NAME"

echo "Installing to /usr/local/bin/$NAME (requires sudo)..."
sudo mv "/tmp/$NAME" /usr/local/bin/$NAME

echo "$NAME installed successfully!"
echo "Run: $NAME filename.txt"
