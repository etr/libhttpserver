"""Scoped Windows owned-child minidump prototype; not a process discovery tool."""
import ctypes as C
from ctypes import wintypes as W
import hashlib
from pathlib import Path
import struct
import time

MAX_BYTES = 32 * 1024 * 1024


def validate_dump(path, expected_pid, expected_created, expected_image):
    """Validate payload, not the writer's exit status; return bounded metadata."""
    path = Path(path)
    size = path.stat().st_size
    if not 32 <= size <= MAX_BYTES:
        raise ValueError('dump size outside bound')
    data = path.read_bytes()
    def bounds(offset, length):
        if offset < 0 or length < 0 or offset > size or length > size-offset:
            raise ValueError('dump RVA outside file')
    def u32(offset):
        bounds(offset, 4)
        return struct.unpack_from('<I', data, offset)[0]
    def location(offset, required=False):
        length, rva = struct.unpack_from('<II', data, offset)
        bounds(rva, length)
        if required and (not length or not rva):
            raise ValueError('required empty location')
        return length, rva
    signature, version, count, directory, _, timestamp, flags = struct.unpack_from('<4sIIIIIQ', data)
    if signature != b'MDMP' or version & 0xffff != 42899 or not 1 <= count <= 1024:
        raise ValueError('invalid dump header')
    if flags & 2:
        raise ValueError('full memory dump prohibited')
    if directory < 32:
        raise ValueError('invalid directory RVA')
    bounds(directory, count * 12)
    streams = {}
    for index in range(count):
        kind, length, rva = struct.unpack_from('<III', data, directory + index*12)
        bounds(rva, length)
        if kind and kind in streams:
            raise ValueError('duplicate stream')
        if kind:
            streams[kind] = (length, rva)
    def required_stream(kind):
        length, rva = streams[kind]
        if length < 4 or rva < 32:
            raise ValueError('required stream empty')
        return length, rva
    length, rva = required_stream(3)
    threads = u32(rva)
    if not 1 <= threads <= 4096 or 4 + threads * 48 > length:
        raise ValueError('invalid thread list')
    thread_info = []
    for index in range(threads):
        row = rva + 4 + index*48
        tid = u32(row)
        stack_bytes, stack_rva = location(row + 32, True)
        context_bytes, context_rva = location(row + 40, True)
        if not tid:
            raise ValueError('empty thread id')
        thread_info.append(dict(id=tid, stack_bytes=stack_bytes, stack_rva=stack_rva,
                                context_bytes=context_bytes, context_rva=context_rva))
    if len({item['id'] for item in thread_info}) != threads:
        raise ValueError('duplicate thread id')
    length, rva = required_stream(4)
    modules = u32(rva)
    if not 1 <= modules <= 4096 or 4 + modules * 108 > length:
        raise ValueError('invalid module list')
    module_info = []
    for index in range(modules):
        row = rva + 4 + index*108
        base, image_size, checksum, image_time, name_rva = struct.unpack_from('<QIIII', data, row)
        name_bytes = u32(name_rva)
        if not base or not image_size or not 0 < name_bytes <= 32768 or name_bytes % 2:
            raise ValueError('invalid module identity')
        bounds(name_rva+4, name_bytes)
        name = data[name_rva+4:name_rva+4+name_bytes].decode('utf-16-le')
        cv_length, cv_rva = location(row+76)
        location(row+84)
        info = dict(path=name, base=base, image_size=image_size, checksum=checksum, timestamp=image_time)
        if cv_length:
            cv = data[cv_rva:cv_rva+cv_length]
            info.update(codeview_bytes=cv_length, codeview_sha256=hashlib.sha256(cv).hexdigest())
            if cv[:4] == b'RSDS' and cv_length >= 24:
                info.update(pdb_guid=cv[4:20].hex(), pdb_age=u32(cv_rva+20),
                            pdb_path=cv[24:].split(b'\0',1)[0].decode('utf-8','replace'))
        module_info.append(info)
    if expected_image.casefold() not in {item['path'].casefold() for item in module_info}:
        raise ValueError('owned executable missing from module list')
    misc_length, misc_rva = required_stream(15)
    if misc_length < 24 or u32(misc_rva) < 24:
        raise ValueError('missing process identity')
    misc_flags = u32(misc_rva+4)
    dumped_pid, dumped_created = u32(misc_rva+8), u32(misc_rva+12)
    if misc_flags & 3 != 3 or dumped_pid != expected_pid or dumped_created != expected_created:
        raise ValueError('dumped process identity mismatch')
    return dict(valid=True, signature='MDMP', size=size, sha256=hashlib.sha256(data).hexdigest(),
                version=version, flags=flags, dump_timestamp=timestamp,
                stream_types=sorted(streams), directory_rva=directory, stream_count=count,
                process_id=dumped_pid, process_created_unix= dumped_created,
                thread_count=threads, threads=thread_info, module_count=modules, modules=module_info)


def windows_api():
    kernel = C.WinDLL('kernel32.dll', use_last_error=True, winmode=0x800)
    def bind(name, result, args):
        fn = getattr(kernel, name)
        fn.restype, fn.argtypes = result, args
        return fn
    bind('GetProcessId', W.DWORD, [W.HANDLE])
    bind('GetProcessTimes', W.BOOL, [W.HANDLE] + [C.POINTER(W.FILETIME)]*4)
    bind('QueryFullProcessImageNameW', W.BOOL, [W.HANDLE, W.DWORD, W.LPWSTR, C.POINTER(W.DWORD)])
    bind('WaitForSingleObject', W.DWORD, [W.HANDLE,W.DWORD])
    bind('TerminateProcess', W.BOOL, [W.HANDLE,W.UINT])
    bind('GetSystemDirectoryW', W.UINT, [W.LPWSTR,W.UINT])
    bind('GetModuleFileNameW', W.DWORD, [W.HMODULE,W.LPWSTR,W.DWORD])
    bind('CreateFileW', W.HANDLE, [W.LPCWSTR,W.DWORD,W.DWORD,C.c_void_p,W.DWORD,W.DWORD,W.HANDLE])
    bind('CloseHandle', W.BOOL, [W.HANDLE])
    bind('SetFilePointerEx', W.BOOL, [W.HANDLE,C.c_longlong,C.POINTER(C.c_longlong),W.DWORD])
    bind('WriteFile', W.BOOL, [W.HANDLE,C.c_void_p,W.DWORD,C.POINTER(W.DWORD),C.c_void_p])
    return kernel


def process_identity(kernel, handle, expected_pid=None):
    pid = kernel.GetProcessId(handle)
    values = [W.FILETIME() for _ in range(4)]
    if not pid or not kernel.GetProcessTimes(handle, *(C.byref(v) for v in values)):
        raise C.WinError(C.get_last_error())
    created = values[0].dwLowDateTime | values[0].dwHighDateTime << 32
    image = C.create_unicode_buffer(32768)
    length = W.DWORD(len(image))
    if not kernel.QueryFullProcessImageNameW(handle, 0, image, C.byref(length)):
        raise C.WinError(C.get_last_error())
    if expected_pid is not None and pid != expected_pid:
        raise ValueError('handle PID mismatch')
    return dict(pid=pid, creation_filetime=created,
                created_unix=created//10_000_000-11_644_473_600, image=image.value)


def capture_minidump(child, expected_identity, path, deadline, kernel):
    """Use this Popen's retained handle only. Caller owns time/job retirement."""
    handle = W.HANDLE(int(child._handle))
    if process_identity(kernel, handle, child.pid) != expected_identity:
        raise ValueError('owned process identity changed')
    if kernel.WaitForSingleObject(handle, 0) != 258:
        raise ValueError('owned child already exited')
    system = C.create_unicode_buffer(32768)
    if not kernel.GetSystemDirectoryW(system,len(system)):
        raise C.WinError(C.get_last_error())
    dllpath = str(Path(system.value) / 'dbghelp.dll')
    dbghelp = C.WinDLL(dllpath, use_last_error=True, winmode=0x800)
    loaded = C.create_unicode_buffer(32768)
    if not kernel.GetModuleFileNameW(dbghelp._handle, loaded, len(loaded)):
        raise C.WinError(C.get_last_error())
    if loaded.value.casefold() != dllpath.casefold():
        raise ValueError('debug DLL path mismatch')
    class IO(C.Structure):
        _pack_ = 4
        _fields_ = [('handle',W.HANDLE),('offset',C.c_uint64),('buffer',C.c_void_p),('length',W.DWORD)]
    class InputPrefix(C.Structure):
        _pack_ = 4
        _fields_ = [('pid',W.DWORD),('process',W.HANDLE),('kind',W.DWORD),('io',IO)]
    # Only the documented prefix/union members actually accessed are modeled.
    assert C.sizeof(C.c_void_p) == 8 and InputPrefix.kind.offset == 12 and InputPrefix.io.offset == 16
    assert IO.offset.offset == 8 and IO.buffer.offset == 16 and IO.length.offset == 24
    CALLBACK = C.WINFUNCTYPE(W.BOOL,C.c_void_p,C.POINTER(InputPrefix),C.c_void_p)
    class CallbackInfo(C.Structure):
        _pack_ = 4
        _fields_ = [('callback',CALLBACK),('parameter',C.c_void_p)]
    # No native direct-write handle is supplied: unsupported callback I/O cannot
    # silently fall back to an uncapped disk writer.
    output = kernel.CreateFileW(str(path),0x40000000,0,None,1,0x80,None)
    if output == W.HANDLE(-1).value:
        raise C.WinError(C.get_last_error())
    state = dict(io_started=False, io_finished=False, writes=0, max_end=0, rejected=False)
    @CALLBACK
    def callback(_, pointer, out):
        try:
            request = pointer.contents
            status = C.cast(out,C.POINTER(C.c_int32))
            if request.kind == 11:
                state['io_started'] = True
                status[0] = 1  # S_FALSE requests all I/O through this callback.
                return True
            if request.kind == 6:
                cancel = C.cast(out,C.POINTER(W.BOOL))
                cancel[0], cancel[1] = True, time.monotonic() >= deadline
                return True
            if request.kind == 12:
                end = request.io.offset + request.io.length
                if (not state['io_started'] or end > MAX_BYTES or time.monotonic() >= deadline):
                    state['rejected'] = True
                    status[0] = -2147024784  # HRESULT_FROM_WIN32(ERROR_DISK_FULL)
                    return False
                written = W.DWORD()
                if not kernel.SetFilePointerEx(output,request.io.offset,None,0):
                    return False
                if not kernel.WriteFile(output,request.io.buffer,request.io.length,C.byref(written),None):
                    return False
                if written.value != request.io.length:
                    return False
                state['writes'] += 1
                state['max_end'] = max(state['max_end'],end)
                status[0] = 0
                return True
            if request.kind == 13:
                state['io_finished'] = True
                status[0] = 0
                return True
            if request.kind in (5,7,9,14):
                return False  # no added memory/kernel dump/ignored read errors
            return True  # preserve initialized default thread/module flags
        except BaseException as exc:
            state['callback_error'] = type(exc).__name__
            return False
    info = CallbackInfo(callback,None)
    writer = dbghelp.MiniDumpWriteDump
    writer.argtypes = [W.HANDLE,W.DWORD,W.HANDLE,W.DWORD,C.c_void_p,C.c_void_p,C.POINTER(CallbackInfo)]
    writer.restype = W.BOOL
    try:
        C.set_last_error(0)
        result = writer(handle, child.pid, None, 0x1000, None, None, C.byref(info))
        error = C.get_last_error()  # immediate, before clocks/logs/other APIs
    finally:
        kernel.CloseHandle(output)
    receipt = dict(api='MiniDumpWriteDump', api_result=bool(result), get_last_error=error,
                   dump_type=0x1000, full_memory=False, dll_path=loaded.value,
                   dll_sha256=hashlib.sha256(Path(loaded.value).read_bytes()).hexdigest(),
                   callback=state, max_bytes=MAX_BYTES, owned_identity=expected_identity)
    if result and state['io_started'] and state['io_finished'] and not state['rejected']:
        try:
            receipt['payload'] = validate_dump(path,child.pid,expected_identity['created_unix'],expected_identity['image'])
            receipt['status'] = 'CAPTURED'
        except (ValueError, KeyError, OSError, UnicodeError, struct.error) as exc:
            receipt['status'] = 'UNAVAILABLE'
            receipt['validation_error'] = str(exc)
    else:
        receipt['status'] = 'UNAVAILABLE'
    return receipt
