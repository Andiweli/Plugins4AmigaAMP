# 🎵 AmigaAMP Visual Plugins

![OS](https://img.shields.io/badge/OS-AmigaOS-blue)
![AI assisted](https://img.shields.io/badge/AI-assisted%20Coding-white)
![GFX](https://img.shields.io/badge/GFX-RTG%2FP96%20needed-orange)

Fullscreen visualizer plugins for **AmigaAMP** on **AmigaOS 3.x / RTG / Picasso96**.

## 🎚️ Plugins

### AmigaAMP-VUMeter

Classic VU meter style visualizer with smooth green/yellow/orange/red bars and red peak markers.

### AmigaAMP-Isometric

Isometric 16x16 spectrum tower visualizer with a blue-to-red color ramp.

## 🖥️ Display

Both plugins use the current Workbench/Public Screen resolution and work nicely on RTG/Picasso96 setups.

## 🔤 Track Info

Centered track information is shown at the top:

* line 1: title / track info
* line 2: artist

Font fallback:

```text
Tahoma > Verdana > TrebuchetMS > Arial > NewTopaz > Topaz
```

## 🛠️ Build

Built with MSYS / AmigaGCC:

```sh
make clean
make
```

## 📦 Requirements

* AmigaOS 3.x
* AmigaAMP
* RTG/Picasso96 recommended
* AmigaGCC for building from source

## 👤 Credits

Created by Andreas "Andiweli" Stürmer.
