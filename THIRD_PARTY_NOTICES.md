# 第三方来源

- TetrisAI / ZouZhiZhang 的 `tetris_ai_runner`，`botris` 分支，固定提交 `ce12b474af8438abec77ae8ec69c8f24a918c7b3`。本项目保留需要的 C++ 源码及已有边界修复；[MIT 许可证](zzztoj_standalone/upstream/LICENSE.md)，[集成说明](src/bot/zzztoj/UPSTREAM.md)，[修复补丁](src/bot/chatgpt/upstream_bounds.patch)。上游：https://github.com/TetrisAI/tetris_ai_runner 。
- MinusKelvin 的 Cold Clear，固定提交 `279edd7c3177ff8077f6a930193397814b281f27`。C++ 移植及参考源模块遵循 [MPL-2.0](third_party/cold-clear/LICENSE)，移植范围见 [说明](docs/BOT_COLDCLEAR.md)。保留用于移植对照与参考值生成的源模块；完整上游：https://github.com/MinusKelvin/cold-clear/tree/279edd7c3177ff8077f6a930193397814b281f27 。
- libcurl、Mbed TLS 与 c-ares：由 CMake 使用系统库或下载固定版本，便携打包脚本收集其许可证；版本和下载校验值见 `cmake/WebSocketDependencies.cmake`。
