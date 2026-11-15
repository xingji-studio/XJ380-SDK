# liballoc-x86_64.a

`third_party/liballoc` submodule 的预编译 fallback（仅在没有 Rust 工具链时使用）。

- 上游：<https://github.com/plos-clan/liballoc>（MIT）
- 固定提交：`6fedb1bbe7b13995f3c37b207a1a325ef1a4d287`
- 归档 SHA-256：`2137fa65410bfecc22371769f3557e8f342216254afe67ad72e76117d1446d08`
- 依赖：`talc 4.4.3`、`spin 0.10.0`
- 来源：plos-clan 的 `release` 资产；OpenXJ380 中的同名文件与之字节级一致。

有 `rustup` / `cargo` 时优先从 submodule 源码构建；没有时使用本文件。
