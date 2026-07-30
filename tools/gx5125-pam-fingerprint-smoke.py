#!/usr/bin/env python3
"""Minimal Linux-PAM authentication client for a fingerprint-only test service."""

from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import os
import sys
from typing import Final

PAM_SUCCESS: Final = 0
PAM_PROMPT_ECHO_OFF: Final = 1
PAM_PROMPT_ECHO_ON: Final = 2
PAM_ERROR_MSG: Final = 3
PAM_TEXT_INFO: Final = 4


class PamMessage(ctypes.Structure):
    _fields_ = [("msg_style", ctypes.c_int), ("msg", ctypes.c_char_p)]


class PamResponse(ctypes.Structure):
    _fields_ = [("resp", ctypes.c_char_p), ("resp_retcode", ctypes.c_int)]


ConversationFn = ctypes.CFUNCTYPE(
    ctypes.c_int,
    ctypes.c_int,
    ctypes.POINTER(ctypes.POINTER(PamMessage)),
    ctypes.POINTER(ctypes.POINTER(PamResponse)),
    ctypes.c_void_p,
)


class PamConv(ctypes.Structure):
    _fields_ = [("conv", ConversationFn), ("appdata_ptr", ctypes.c_void_p)]


def load_library(name: str, fallback: str) -> ctypes.CDLL:
    resolved = ctypes.util.find_library(name) or fallback
    try:
        return ctypes.CDLL(resolved)
    except OSError as exc:
        raise RuntimeError(f"cannot load {resolved}: {exc}") from exc


def main() -> int:
    parser = argparse.ArgumentParser(add_help=True)
    parser.add_argument("--service", required=True)
    parser.add_argument("--user", required=True)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()

    if not args.service or "/" in args.service or "\x00" in args.service:
        print("GOODIX_BETA_PAM_HELPER=FAIL reason:invalid-service", file=sys.stderr)
        return 2
    if not args.user or "\x00" in args.user:
        print("GOODIX_BETA_PAM_HELPER=FAIL reason:invalid-user", file=sys.stderr)
        return 2

    try:
        pam = load_library("pam", "libpam.so.0")
        libc = load_library("c", "libc.so.6")
    except RuntimeError as exc:
        print(f"GOODIX_BETA_PAM_HELPER=FAIL reason:library-load detail:{exc}", file=sys.stderr)
        return 2

    libc.calloc.argtypes = [ctypes.c_size_t, ctypes.c_size_t]
    libc.calloc.restype = ctypes.c_void_p
    libc.strdup.argtypes = [ctypes.c_char_p]
    libc.strdup.restype = ctypes.c_void_p
    libc.free.argtypes = [ctypes.c_void_p]
    libc.free.restype = None

    pam.pam_start.argtypes = [
        ctypes.c_char_p,
        ctypes.c_char_p,
        ctypes.POINTER(PamConv),
        ctypes.POINTER(ctypes.c_void_p),
    ]
    pam.pam_start.restype = ctypes.c_int
    pam.pam_authenticate.argtypes = [ctypes.c_void_p, ctypes.c_int]
    pam.pam_authenticate.restype = ctypes.c_int
    pam.pam_end.argtypes = [ctypes.c_void_p, ctypes.c_int]
    pam.pam_end.restype = ctypes.c_int
    pam.pam_strerror.argtypes = [ctypes.c_void_p, ctypes.c_int]
    pam.pam_strerror.restype = ctypes.c_char_p

    username = args.user.encode("utf-8")

    @ConversationFn
    def conversation(
        num_msg: int,
        messages: ctypes.POINTER(ctypes.POINTER(PamMessage)),
        responses: ctypes.POINTER(ctypes.POINTER(PamResponse)),
        _appdata: ctypes.c_void_p,
    ) -> int:
        if num_msg <= 0 or num_msg > 64 or not messages or not responses:
            return 19  # PAM_CONV_ERR

        raw = libc.calloc(num_msg, ctypes.sizeof(PamResponse))
        if not raw:
            return 5  # PAM_BUF_ERR
        array = ctypes.cast(raw, ctypes.POINTER(PamResponse))

        try:
            for index in range(num_msg):
                message_ptr = messages[index]
                if not message_ptr:
                    raise ValueError("null PAM message")
                message = message_ptr.contents
                text = message.msg.decode("utf-8", errors="replace") if message.msg else ""

                if message.msg_style == PAM_PROMPT_ECHO_ON:
                    duplicated = libc.strdup(username)
                    if not duplicated:
                        raise MemoryError("strdup failed")
                    array[index].resp = ctypes.cast(duplicated, ctypes.c_char_p)
                elif message.msg_style == PAM_PROMPT_ECHO_OFF:
                    # Fingerprint-only test service must never need a password.
                    duplicated = libc.strdup(b"")
                    if not duplicated:
                        raise MemoryError("strdup failed")
                    array[index].resp = ctypes.cast(duplicated, ctypes.c_char_p)
                elif message.msg_style in (PAM_ERROR_MSG, PAM_TEXT_INFO):
                    if text:
                        print(text, flush=True)
                    array[index].resp = None
                else:
                    raise ValueError(f"unsupported PAM message style {message.msg_style}")
                array[index].resp_retcode = 0
        except (ValueError, MemoryError):
            for index in range(num_msg):
                if array[index].resp:
                    libc.free(ctypes.cast(array[index].resp, ctypes.c_void_p))
                    array[index].resp = None
            libc.free(raw)
            return 19

        responses[0] = ctypes.cast(raw, ctypes.POINTER(PamResponse))
        return PAM_SUCCESS

    if args.selftest:
        print(
            "GOODIX_BETA_PAM_HELPER_SELFTEST=PASS "
            "ctypes_pam:1 password_input:0 conversation:1"
        )
        return 0

    handle = ctypes.c_void_p()
    conv = PamConv(conversation, None)
    rc = pam.pam_start(args.service.encode("ascii"), username, ctypes.byref(conv), ctypes.byref(handle))
    if rc != PAM_SUCCESS:
        detail = pam.pam_strerror(handle, rc)
        text = detail.decode("utf-8", errors="replace") if detail else "unknown"
        print(f"GOODIX_BETA_PAM_AUTH=FAIL stage:pam-start rc:{rc} detail:{text}", file=sys.stderr)
        return 1

    auth_rc = pam.pam_authenticate(handle, 0)
    detail = pam.pam_strerror(handle, auth_rc)
    text = detail.decode("utf-8", errors="replace") if detail else "unknown"
    pam.pam_end(handle, auth_rc)

    if auth_rc != PAM_SUCCESS:
        print(f"GOODIX_BETA_PAM_AUTH=FAIL stage:pam-authenticate rc:{auth_rc} detail:{text}", file=sys.stderr)
        return 1

    print(
        "GOODIX_BETA_PAM_AUTH=PASS "
        f"service:{args.service} user:{args.user} fingerprint_only:1 password_input:0"
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        print("GOODIX_BETA_PAM_AUTH=FAIL stage:interrupted", file=sys.stderr)
        raise SystemExit(130)
