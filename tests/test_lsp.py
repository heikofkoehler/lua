#!/usr/bin/env python3
import subprocess
import json
import sys
import os

def send_rpc(proc, msg):
    payload = json.dumps(msg)
    header = f"Content-Length: {len(payload)}\r\n\r\n"
    proc.stdin.write((header + payload).encode('utf-8'))
    proc.stdin.flush()

def read_rpc(proc):
    content_length = -1
    while True:
        line = proc.stdout.readline().decode('utf-8')
        if not line:
            return None
        line = line.strip('\r\n')
        if not line:
            break
        if line.lower().startswith('content-length:'):
            content_length = int(line.split(':')[1].strip())
    
    if content_length < 0:
        return None
    data = proc.stdout.read(content_length).decode('utf-8')
    return json.loads(data)

def main():
    lsp_bin = sys.argv[1] if len(sys.argv) > 1 else "./build/lua-lsp"
    if not os.path.exists(lsp_bin):
        print(f"Error: LSP binary '{lsp_bin}' not found")
        sys.exit(1)

    print("=== Running LSP Integration Tests ===")

    proc = subprocess.Popen(
        [lsp_bin, "--stdio"],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    try:
        # Test 1: initialize request
        print("Test 1: initialize handshake... ", end="", flush=True)
        send_rpc(proc, {
            "jsonrpc": "2.0",
            "id": 1,
            "method": "initialize",
            "params": {
                "processId": os.getpid(),
                "rootUri": "file:///workspace",
                "capabilities": {}
            }
        })
        resp = read_rpc(proc)
        assert resp["id"] == 1
        caps = resp["result"]["capabilities"]
        assert caps["hoverProvider"] is True
        assert caps["definitionProvider"] is True
        assert caps["documentSymbolProvider"] is True
        assert "completionProvider" in caps
        print("PASS")

        # Test 2: initialized notification
        print("Test 2: initialized notification... ", end="", flush=True)
        send_rpc(proc, {
            "jsonrpc": "2.0",
            "method": "initialized",
            "params": {}
        })
        print("PASS")

        # Test 3: didOpen with valid file -> expect clean diagnostics
        print("Test 3: textDocument/didOpen (valid file)... ", end="", flush=True)
        uri = "file:///workspace/test.lua"
        lua_code = (
            "-- Calculate sum of two numbers\n"
            "function add(a, b)\n"
            "    return a + b\n"
            "end\n"
            "\n"
            "local result = add(10, 20)\n"
            "print(result)\n"
        )
        send_rpc(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didOpen",
            "params": {
                "textDocument": {
                    "uri": uri,
                    "languageId": "lua",
                    "version": 1,
                    "text": lua_code
                }
            }
        })
        diag_msg = read_rpc(proc)
        assert diag_msg["method"] == "textDocument/publishDiagnostics"
        assert diag_msg["params"]["uri"] == uri
        assert len(diag_msg["params"]["diagnostics"]) == 0
        print("PASS")

        # Test 4: documentSymbol outline
        print("Test 4: textDocument/documentSymbol... ", end="", flush=True)
        send_rpc(proc, {
            "jsonrpc": "2.0",
            "id": 2,
            "method": "textDocument/documentSymbol",
            "params": {
                "textDocument": {"uri": uri}
            }
        })
        resp = read_rpc(proc)
        assert resp["id"] == 2
        symbols = resp["result"]
        names = [s["name"] for s in symbols]
        assert "add" in names
        assert "result" in names
        print("PASS")

        # Test 5: hover provider (user function and stdlib)
        print("Test 5: textDocument/hover... ", end="", flush=True)
        # Hover on 'add' (line 1, col 10)
        send_rpc(proc, {
            "jsonrpc": "2.0",
            "id": 3,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": uri},
                "position": {"line": 1, "character": 10}
            }
        })
        resp = read_rpc(proc)
        assert resp["id"] == 3
        hover_text = resp["result"]["contents"]["value"]
        assert "function add(a, b)" in hover_text
        assert "Calculate sum of two numbers" in hover_text

        # Hover on 'print' (line 6, col 2)
        send_rpc(proc, {
            "jsonrpc": "2.0",
            "id": 4,
            "method": "textDocument/hover",
            "params": {
                "textDocument": {"uri": uri},
                "position": {"line": 6, "character": 2}
            }
        })
        resp = read_rpc(proc)
        assert resp["id"] == 4
        hover_text = resp["result"]["contents"]["value"]
        assert "print(...)" in hover_text
        print("PASS")

        # Test 6: definition provider
        print("Test 6: textDocument/definition... ", end="", flush=True)
        # Definition of 'result' in 'print(result)' (line 6, col 8)
        send_rpc(proc, {
            "jsonrpc": "2.0",
            "id": 5,
            "method": "textDocument/definition",
            "params": {
                "textDocument": {"uri": uri},
                "position": {"line": 6, "character": 8}
            }
        })
        resp = read_rpc(proc)
        assert resp["id"] == 5
        loc = resp["result"]
        assert loc["uri"] == uri
        assert loc["range"]["start"]["line"] == 5  # local result defined on line 5
        print("PASS")

        # Test 7: completion provider (stdlib and keywords)
        print("Test 7: textDocument/completion... ", end="", flush=True)
        # Completion for table.
        lua_code2 = "local t = table."
        send_rpc(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didChange",
            "params": {
                "textDocument": {"uri": uri, "version": 2},
                "contentChanges": [{"text": lua_code2}]
            }
        })
        # consume publishDiagnostics notification from didChange
        read_rpc(proc)

        send_rpc(proc, {
            "jsonrpc": "2.0",
            "id": 6,
            "method": "textDocument/completion",
            "params": {
                "textDocument": {"uri": uri},
                "position": {"line": 0, "character": 16}
            }
        })
        resp = read_rpc(proc)
        assert resp["id"] == 6
        items = [item["label"] for item in resp["result"]["items"]]
        assert "insert" in items
        assert "concat" in items
        assert "sort" in items
        print("PASS")

        # Test 8: didChange with syntax error -> verify diagnostic
        print("Test 8: syntax error diagnostics... ", end="", flush=True)
        broken_code = "function invalid(\n"
        send_rpc(proc, {
            "jsonrpc": "2.0",
            "method": "textDocument/didChange",
            "params": {
                "textDocument": {"uri": uri, "version": 3},
                "contentChanges": [{"text": broken_code}]
            }
        })
        diag_msg = read_rpc(proc)
        assert diag_msg["method"] == "textDocument/publishDiagnostics"
        diags = diag_msg["params"]["diagnostics"]
        assert len(diags) > 0
        assert diags[0]["severity"] == 1  # Error
        print("PASS")

        # Test 9: shutdown and exit
        print("Test 9: shutdown & exit... ", end="", flush=True)
        send_rpc(proc, {
            "jsonrpc": "2.0",
            "id": 7,
            "method": "shutdown",
            "params": {}
        })
        resp = read_rpc(proc)
        assert resp["id"] == 7
        assert resp["result"] is None

        send_rpc(proc, {
            "jsonrpc": "2.0",
            "method": "exit",
            "params": {}
        })
        proc.wait(timeout=5)
        assert proc.returncode == 0
        print("PASS")

        print("=== All LSP Tests Passed! ===")

    finally:
        if proc.poll() is None:
            proc.kill()

if __name__ == "__main__":
    main()
