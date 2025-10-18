#!/bin/bash

# Quick demo script showing both versions

echo "╔══════════════════════════════════════════╗"
echo "║   Lunar Surface Viewer - Quick Demo     ║"
echo "╚══════════════════════════════════════════╝"
echo ""

# Check data
if [ ! -f ".data/SLDEM2015_512_00N_30N_000_045_FLOAT.IMG" ]; then
    echo "❌ No data files found!"
    echo "   Run: python schema.py"
    exit 1
fi

# Check builds
CPP_BUILT=false
if [ -f "./lunar_viewer" ]; then
    CPP_BUILT=true
fi

echo "📊 Available Viewers:"
echo ""
echo "  1. Python PyVista  - Easy to use, slower (~30 FPS)"
echo "  2. C++ OpenGL      - Game-like, fast (60-300+ FPS)"
echo ""

if [ "$CPP_BUILT" = false ]; then
    echo "⚠️  C++ version not built yet. Building now..."
    make
    echo ""
fi

echo "Choose version to run:"
echo "  [1] Python (viewer.py)"
echo "  [2] C++ (lunar_viewer) ⚡ RECOMMENDED"
echo "  [3] Python demo (save images only)"
echo "  [q] Quit"
echo ""
read -p "Your choice: " choice

case $choice in
    1)
        echo ""
        echo "🐍 Starting Python viewer..."
        echo "   (Mouse to rotate/pan/zoom, 'q' to quit)"
        python viewer.py
        ;;
    2)
        echo ""
        echo "🚀 Starting C++ OpenGL viewer..."
        echo "   Controls:"
        echo "   • WASD: Move   • Mouse: Look   • Q/E: Up/Down"
        echo "   • Tab: Wireframe   • Shift: Sprint   • ESC: Quit"
        echo ""
        ./lunar_viewer
        ;;
    3)
        echo ""
        echo "📸 Generating images with Python..."
        python demo.py
        echo ""
        echo "✓ Images saved:"
        ls -lh *.png 2>/dev/null | awk '{print "   " $9 " (" $5 ")"}'
        ;;
    *)
        echo "Goodbye!"
        ;;
esac
