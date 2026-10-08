// Copyright (C) 2026 Sebastiano Merlino; SPDX-License-Identifier: LGPL-2.1-or-later
package main

import (
	"bufio"
	"context"
	"crypto/tls"
	"crypto/x509"
	"encoding/hex"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	quic "github.com/quic-go/quic-go"
	"github.com/quic-go/quic-go/http3"
	"github.com/quic-go/quic-go/logging"
	"github.com/quic-go/quic-go/qlog"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"runtime/debug"
	"time"
)

type fields = map[string]any

var input = bufio.NewScanner(os.Stdin)

func identity() fields {
	info, ok := debug.ReadBuildInfo()
	if !ok {
		panic("Go build identity missing")
	}
	version := ""
	for _, dep := range info.Deps {
		if dep.Path == "github.com/quic-go/quic-go" && dep.Replace == nil {
			version = dep.Version
		}
	}
	return fields{"client": "quic-go", "version": version, "go": info.GoVersion}
}
func control(command string, values fields) (fields, error) {
	values["command"] = command
	if err := json.NewEncoder(os.Stdout).Encode(values); err != nil {
		return nil, err
	}
	if !input.Scan() {
		return nil, fmt.Errorf("runner control EOF")
	}
	result := fields{}
	err := json.Unmarshal(input.Bytes(), &result)
	return result, err
}
func open(client *http3.ClientConn, ctx context.Context, port int, method, path string, body []byte) (*http3.RequestStream, error) {
	stream, err := client.OpenRequestStream(ctx)
	if err != nil {
		return nil, err
	}
	request, err := http.NewRequestWithContext(ctx, method, fmt.Sprintf("https://localhost:%d%s", port, path), nil)
	if err != nil {
		return nil, err
	}
	request.ContentLength = int64(len(body))
	request.Body = http.NoBody
	if err = stream.SendRequestHeader(request); err != nil {
		return nil, err
	}
	for offset := 0; offset < len(body); offset += 3 {
		end := offset + 3
		if end > len(body) {
			end = len(body)
		}
		if _, err = stream.Write(body[offset:end]); err != nil {
			return nil, err
		}
	}
	if err = stream.Close(); err != nil {
		return nil, err
	}
	return stream, nil
}
func response(stream *http3.RequestStream) (fields, error) {
	res, err := stream.ReadResponse()
	if err != nil {
		return nil, err
	}
	defer res.Body.Close()
	body, err := io.ReadAll(io.LimitReader(res.Body, 65537))
	if err != nil {
		return nil, err
	}
	if len(body) > 65536 {
		return nil, fmt.Errorf("response body bound")
	}
	return fields{"status": res.StatusCode, "body_hex": hex.EncodeToString(body), "stream": int64(stream.StreamID())}, nil
}
func roots(path string) (*x509.CertPool, error) {
	cert, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}
	roots := x509.NewCertPool()
	if !roots.AppendCertsFromPEM(cert) {
		return nil, fmt.Errorf("invalid fixture CA")
	}
	return roots, nil
}
func run() error {
	if len(os.Args) == 2 && os.Args[1] == "--identity" {
		return json.NewEncoder(os.Stdout).Encode(identity())
	}
	port := flag.Int("port", 0, "fixture port")
	ca := flag.String("ca", "", "fixture CA")
	wrongCA := flag.String("wrong-ca", "", "untrusted CA")
	logs := flag.String("log-dir", "", "diagnostic directory")
	flag.Parse()
	trust, err := roots(*ca)
	if err != nil {
		return err
	}
	if err = os.MkdirAll(*logs, 0700); err != nil {
		return err
	}
	keys, err := os.OpenFile(filepath.Join(*logs, "tls.keys"), os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0600)
	if err != nil {
		return err
	}
	defer keys.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 100*time.Second)
	defer cancel()
	config := &quic.Config{Versions: []quic.Version{quic.Version1}, HandshakeIdleTimeout: 10 * time.Second, MaxIdleTimeout: 15 * time.Second, Tracer: func(ctx context.Context, p logging.Perspective, id quic.ConnectionID) *logging.ConnectionTracer {
		file, err := os.OpenFile(filepath.Join(*logs, fmt.Sprintf("qlog-%s.sqlog", id)), os.O_CREATE|os.O_TRUNC|os.O_WRONLY, 0600)
		if err != nil {
			panic(err)
		}
		return qlog.NewConnectionTracer(file, p, id)
	}}
	tlsConfig := &tls.Config{RootCAs: trust, ServerName: "localhost", NextProtos: []string{"h3"}, MinVersion: tls.VersionTLS13, KeyLogWriter: keys}
	address := fmt.Sprintf("127.0.0.1:%d", *port)
	conn, err := quic.DialAddr(ctx, address, tlsConfig, config)
	if err != nil {
		return err
	}
	defer conn.CloseWithError(0, "done")
	state := conn.ConnectionState()
	cases := []fields{{"case": "handshake", "alpn": state.TLS.NegotiatedProtocol, "quic_version": uint32(state.Version), "tls_version": "TLSv1.3", "verified": state.TLS.Version == tls.VersionTLS13 && len(state.TLS.VerifiedChains) > 0, "early_data": state.Used0RTT}}
	transport := &http3.Transport{}
	client := transport.NewClientConn(conn)
	upload := []byte("first\x00second\xffthird\x00")
	for _, item := range []struct {
		name, method, path string
		body               []byte
	}{{"get", "GET", "/hello", nil}, {"post", "POST", "/echo", upload}} {
		stream, err := open(client, ctx, *port, item.method, item.path, item.body)
		if err != nil {
			return err
		}
		result, err := response(stream)
		if err != nil {
			return err
		}
		result["case"] = item.name
		cases = append(cases, result)
	}
	held, err := open(client, ctx, *port, "GET", "/hold", nil)
	if err != nil {
		return err
	}
	observed, err := control("wait", fields{"event": "held", "stream": int64(held.StreamID())})
	if err != nil {
		return err
	}
	health, err := open(client, ctx, *port, "GET", "/health", nil)
	if err != nil {
		return err
	}
	healthy, err := response(health)
	if err != nil {
		return err
	}
	if _, err = control("release", fields{"stream": int64(held.StreamID())}); err != nil {
		return err
	}
	released, err := response(held)
	if err != nil {
		return err
	}
	cases = append(cases, fields{"case": "concurrency", "held_stream": int64(held.StreamID()), "health_stream": int64(health.StreamID()), "health_status": healthy["status"], "hold_status": released["status"], "health_before_release": true, "connection": observed["connection"]})
	if _, err = control("reset", fields{}); err != nil {
		return err
	}
	stopped, err := open(client, ctx, *port, "GET", "/hold", nil)
	if err != nil {
		return err
	}
	if _, err = control("wait", fields{"event": "held", "stream": int64(stopped.StreamID())}); err != nil {
		return err
	}
	stopped.CancelRead(quic.StreamErrorCode(http3.ErrCodeRequestCanceled))
	stopped.CancelWrite(quic.StreamErrorCode(http3.ErrCodeRequestCanceled))
	_, err = stopped.ReadResponse()
	var streamError *quic.StreamError
	if !errors.As(err, &streamError) {
		return fmt.Errorf("expected native stream cancellation, got %T", err)
	}
	observed, err = control("wait", fields{"event": "cancelled", "stream": int64(stopped.StreamID())})
	if err != nil {
		return err
	}
	sibling, err := open(client, ctx, *port, "GET", "/hello", nil)
	if err != nil {
		return err
	}
	siblingResult, err := response(sibling)
	if err != nil {
		return err
	}
	cases = append(cases, fields{"case": "cancellation", "stream": int64(stopped.StreamID()), "type": "*quic.StreamError", "code": uint64(streamError.ErrorCode), "sibling_status": siblingResult["status"], "server_cancelled": observed["event"] == "cancelled"})
	missing, err := open(client, ctx, *port, "GET", "/missing", nil)
	if err != nil {
		return err
	}
	missingResult, err := response(missing)
	if err != nil {
		return err
	}
	cases = append(cases, fields{"case": "missing", "status": missingResult["status"]})
	conn.CloseWithError(0, "success matrix complete")
	wrong, err := roots(*wrongCA)
	if err != nil {
		return err
	}
	tlsConfig = tlsConfig.Clone()
	tlsConfig.RootCAs = wrong
	bad, err := quic.DialAddr(ctx, address, tlsConfig, config)
	if err == nil {
		bad.CloseWithError(0, "unexpected success")
		return fmt.Errorf("wrong CA succeeded")
	}
	var verification *tls.CertificateVerificationError
	var transportError *quic.TransportError
	if !errors.As(err, &verification) || !errors.As(err, &transportError) {
		return fmt.Errorf("expected verification failure, got %T", err)
	}
	cases = append(cases, fields{"case": "tls_failure", "type": "*tls.CertificateVerificationError", "code": uint64(transportError.ErrorCode), "response": false})
	receipt := identity()
	receipt["cases"] = cases
	return json.NewEncoder(os.Stdout).Encode(fields{"receipt": receipt})
}
func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
