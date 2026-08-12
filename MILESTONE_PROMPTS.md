# CoreDesk Codex Milestone Prompts

使用方式：每次只复制一个里程碑提示词给 Codex，并同时保留 `docs/COREDESK_SPEC.md` 在仓库中。

## M0
实现 M0 - 工程骨架。只实现 M0。先读取规范第 0、3、5、6、7、8、23、26 节。完成 CMake targets、common Result/Error、GoogleTest 骨架和 `coredesk_cli --version`。严格按 M0 DoD 验收，更新 IMPLEMENTATION_STATUS.md 后停止。

## M1
实现 M1 - ThreadPool + FileScanner CLI。只实现 M1。重点读取规范第 8-10、19、21、23 节。不得开始 Index/Search。完成 bounded ThreadPool、Cancellation、FileRecord、FileScanner、CLI scan 和对应测试。按 M1 DoD 验收后停止。

## M2
实现 M2 - IndexBuilder + SearchEngine + LRU。只实现 M2。重点读取规范第 11、12、21、22、23 节。实现 immutable snapshot、token index、sorted prefix search、substring fallback、LRU 和 bench_search。不得引入 SQLite/FTS/Trie 第三方库。按 M2 DoD 验收后停止。

## M3
实现 M3 - FrameProtocol。只实现 M3。重点读取规范第 14、15、21、23 节。24-byte header 和 MessageType 数值不得修改。FrameDecoder 必须覆盖 partial header、partial payload、multi-frame、bad magic/version、oversize。按 M3 DoD 验收后停止。

## M4
实现 M4 - Service + QLocal IPC。只实现 M4。重点读取规范第 13、15、16、19、21、23 节。实现 ServiceController、snapshot swap、QLocalServer/QLocalSocket adapter、Ping/Scan/Search 集成测试。Core 公共头文件不得包含 Qt。按 M4 DoD 验收后停止。

## M5
实现 M5 - Qt Desktop Thin UI。只实现 M5。重点读取规范第 16、17、19、23 节。UI 只能通过 IPC 工作；不得直接调用 FileScanner/SearchEngine。实现 150ms debounce、request_id 防旧响应覆盖、service auto-start/reconnect。按 M5 DoD 验收后停止。

## M6
实现 M6 - TCP LAN Transfer。只实现 M6。重点读取规范第 14、18、19、21、23 节。使用 QTcpServer/QTcpSocket；按固定 FileOffer/FileChunk 规范；分块、.part、SHA-256、TargetExists、path traversal 防护。不得做断点续传/自动发现/TLS。按 M6 DoD 验收后停止。

## M7
实现 M7 - 稳定性与性能。只实现 M7。重点读取规范第 20-23 节。补日志、sanitizer CMake option、压力测试、benchmark 记录和真实 BUG_POSTMORTEM。不得伪造性能数据。按 M7 DoD 验收后停止。

## M8
实现 M8 - 跨平台与求职包装。只实现 M8。重点读取规范第 23-25、29-31 节。修第二平台编译问题，完善 README、架构/线程/协议说明和演示脚本；不得提前做 v1.1 功能。按 M8 DoD 验收后停止。
