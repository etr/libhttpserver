# RFC 9113 request-engine corpus

The five-column shared corpus format is unchanged. `verdict` contains
`scope code stream`, with RFC wire error codes. The payload column lists routed
stream IDs; `/` identifies stream 1 and `/sibling` identifies stream 3.
The stage `eof` requires EOF during a field block. `unadvertised` withholds
server SETTINGS exposure to exercise RFC 8441 capability gating.

Binary HPACK literals are authored independently of the library encoder. The
harness checks resets, GOAWAY, response order, sticky failure, consumption,
routes and complete budget release. Coalesced, bytewise, every split and partial
output retirement must give identical wire output. Body-error fixtures include
routing of their initially valid opening request before the later error.

RFC 9113 sections 5.3.2, 6.2, 6.3 and Appendix B deprecate priority dependency
semantics. Self-dependency therefore has no error verdict. PRIORITY length,
nonzero stream placement and continuation isolation remain enforced.

Required minimum/final HPACK table updates after acknowledged SETTINGS changes
are covered by `hpack_corpus` using independent octets; peer SETTINGS affects the
response encoder, so feeding those settings to this engine cannot emulate local
receive-table changes. The existing 28 connection-level fixtures remain separate.

The TLS fixture exercises the real private request engine. The public listener
still dispatches HTTP/1. No transcript substitutes for independent clients.
