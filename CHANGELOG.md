# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.0] - 2026-06-22
### Changed
- Complete architectural refactor introducing the `SparkCore` layer.
- Modularized global code into State Manager, Hardware Manager, Face Manager, Animation Manager, Emotion Manager, and Intent Manager.
- Replaced direct LVGL widget polling and global scope manipulation with decoupled event-driven API.
- Fixed OOM and rendering crash issues by storing face configurations in static const flash arrays.
- Stabilized FPS by reusing underlying visual containers and cleanly abstracting animation callback execution.
- Relocated ESP-IDF source files into `firmware/` subdirectory for clean structural layout.
