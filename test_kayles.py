import os
import math
import socket
import struct
import subprocess
import time
import unittest
from contextlib import closing

SERVER_CANDIDATES = ("./kayles_server", "./server_exec")
CLIENT_CANDIDATES = ("./kayles_client", "./client_exec")

HOST = "127.0.0.1"

MSG_JOIN = 0
MSG_MOVE_1 = 1
MSG_MOVE_2 = 2
MSG_KEEP_ALIVE = 3
MSG_GIVE_UP = 4

WAITING_FOR_OPPONENT = 0
TURN_A = 1
TURN_B = 2
WIN_A = 3
WIN_B = 4


def pick_existing_executable(candidates):
    for path in candidates:
        if os.path.isfile(path) and os.access(path, os.X_OK):
            return path
    return candidates[0]


SERVER_BIN = pick_existing_executable(SERVER_CANDIDATES)
CLIENT_BIN = pick_existing_executable(CLIENT_CANDIDATES)


def free_udp_port():
    with closing(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) as s:
        s.bind((HOST, 0))
        return s.getsockname()[1]


def pack_client_msg(msg_type, player_id, game_id=0, pawn=0):
    if msg_type == MSG_JOIN:
        return struct.pack("!BI", msg_type, player_id)
    if msg_type in (MSG_MOVE_1, MSG_MOVE_2):
        return struct.pack("!BII B".replace(" ", ""), msg_type, player_id, game_id, pawn)
    if msg_type in (MSG_KEEP_ALIVE, MSG_GIVE_UP):
        return struct.pack("!BII", msg_type, player_id, game_id)
    raise ValueError(f"unknown msg_type: {msg_type}")


def pack_game_state(game_id, player_a_id, player_b_id, status, max_pawn, pawn_row):
    return struct.pack("!I I I B B".replace(" ", ""), game_id, player_a_id, player_b_id, status, max_pawn) + pawn_row


def unpack_game_state(data):
    if data is None:
        raise ValueError("no data")
    if len(data) == 14 and data[12] == 255:
        raise ValueError("received WRONG_MSG instead of GAME_STATE")
    if len(data) < 14:
        raise ValueError("data too short")
    game_id, player_a_id, player_b_id, status, max_pawn = struct.unpack("!I I I B B".replace(" ", ""), data[:14])
    expected_len = 14 + (max_pawn // 8) + 1
    if len(data) != expected_len:
        raise ValueError(f"expected {expected_len} bytes, got {len(data)}")
    return {
        "game_id": game_id,
        "player_a_id": player_a_id,
        "player_b_id": player_b_id,
        "status": status,
        "max_pawn": max_pawn,
        "pawn_row": data[14:],
    }


def unpack_wrong_msg(data):
    if data is None:
        raise ValueError("no data")
    if len(data) != 14:
        raise ValueError(f"WRONG_MSG must be exactly 14 bytes, got {len(data)}")
    orig_msg = data[:12]
    status, error_index = struct.unpack("!BB", data[12:14])
    return {
        "orig_msg": orig_msg,
        "status": status,
        "error_index": error_index,
    }


def terminate_process(proc):
    if proc is None:
        return
    if proc.poll() is None:
        try:
            proc.terminate()
            proc.wait(timeout=1)
        except subprocess.TimeoutExpired:
            try:
                proc.kill()
                proc.wait(timeout=1)
            except Exception:
                pass


class KaylesTestBase(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.isfile(SERVER_BIN):
            raise FileNotFoundError(f"{SERVER_BIN} not found")
        if not os.path.isfile(CLIENT_BIN):
            raise FileNotFoundError(f"{CLIENT_BIN} not found")

    def launch_server(self, row="11111", timeout=2, port=None, addr=HOST):
        if port is None:
            port = free_udp_port()

        proc = subprocess.Popen(
            [SERVER_BIN, "-r", row, "-a", addr, "-p", str(port), "-t", str(timeout)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        time.sleep(0.15)

        if proc.poll() is not None:
            err = proc.stderr.read().decode("utf-8", errors="replace")
            self.fail(f"server failed to start: {err.strip()}")

        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.settimeout(0.8)

        self.addCleanup(sock.close)
        self.addCleanup(terminate_process, proc)
        return proc, sock, port

    def send_recv(self, sock, port, payload):
        sock.sendto(payload, (HOST, port))
        try:
            data, _ = sock.recvfrom(4096)
            return data
        except socket.timeout:
            return None

    def assertWrongMsg(self, data, prefix=None, status=255, error_index=None):
        msg = unpack_wrong_msg(data)
        self.assertEqual(msg["status"], status)
        if prefix is not None:
            self.assertEqual(msg["orig_msg"][: len(prefix)], prefix)
        if error_index is not None:
            self.assertEqual(msg["error_index"], error_index)
        self.assertLess(msg["error_index"], 12)
        return msg


class TestKaylesServerProtocol(KaylesTestBase):
    def test_join_creates_waiting_game(self):
        _, sock, port = self.launch_server(row="11111", timeout=2)
        req = pack_client_msg(MSG_JOIN, 100)
        resp = self.send_recv(sock, port, req)
        state = unpack_game_state(resp)

        self.assertEqual(state["player_a_id"], 100)
        self.assertEqual(state["player_b_id"], 0)
        self.assertEqual(state["status"], WAITING_FOR_OPPONENT)
        self.assertEqual(state["max_pawn"], 4)
        self.assertEqual(state["pawn_row"], b"\xF8")  # 11111000

    def test_second_join_completes_game_and_same_player_as_both_sides(self):
        _, sock, port = self.launch_server(row="11111", timeout=2)

        first = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 101)))
        self.assertEqual(first["status"], WAITING_FOR_OPPONENT)
        game_id = first["game_id"]

        second = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 101)))
        self.assertEqual(second["game_id"], game_id)
        self.assertEqual(second["player_a_id"], 101)
        self.assertEqual(second["player_b_id"], 101)
        self.assertEqual(second["status"], TURN_B)

    def test_legal_moves_and_state_transitions(self):
        _, sock, port = self.launch_server(row="11111", timeout=2)

        g = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 101)))
        game_id = g["game_id"]
        unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 202)))

        # B removes pawn 0 -> 01111000
        s1 = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_MOVE_1, 202, game_id, 0)))
        self.assertEqual(s1["status"], TURN_A)
        self.assertEqual(s1["pawn_row"], b"\x78")

        # A removes pawns 3 and 4 -> 01100000
        s2 = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_MOVE_2, 101, game_id, 3)))
        self.assertEqual(s2["status"], TURN_B)
        self.assertEqual(s2["pawn_row"], b"\x60")

    def test_illegal_move_wrong_turn_keeps_state(self):
        _, sock, port = self.launch_server(row="11111", timeout=2)

        g = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 1)))
        game_id = g["game_id"]
        unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 2)))

        # It is B's turn; A tries to move.
        resp = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_MOVE_1, 1, game_id, 1)))
        self.assertEqual(resp["status"], TURN_B)
        self.assertEqual(resp["pawn_row"], b"\xF8")

    def test_illegal_move_on_already_removed_pawn_keeps_state(self):
        _, sock, port = self.launch_server(row="11111", timeout=2)

        g = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 11)))
        game_id = g["game_id"]
        unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 22)))

        # Legal move by B: remove pawn 0.
        s1 = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_MOVE_1, 22, game_id, 0)))
        self.assertEqual(s1["pawn_row"], b"\x78")
        self.assertEqual(s1["status"], TURN_A)

        # A now tries to remove the already absent pawn 0.
        s2 = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_MOVE_1, 11, game_id, 0)))
        self.assertEqual(s2["pawn_row"], b"\x78")
        self.assertEqual(s2["status"], TURN_A)

    def test_move_two_out_of_bounds_is_legal_message_but_illegal_move(self):
        _, sock, port = self.launch_server(row="11111", timeout=2)

        g = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 11)))
        game_id = g["game_id"]
        unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 22)))

        # B's turn. pawn=4 implies pawn+1 is out of range.
        resp = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_MOVE_2, 22, game_id, 4)))
        self.assertEqual(resp["status"], TURN_B)
        self.assertEqual(resp["pawn_row"], b"\xF8")

    def test_wrong_message_type_is_rejected(self):
        _, sock, port = self.launch_server(row="11111", timeout=2)

        bad_req = struct.pack("!BII B".replace(" ", ""), 99, 100, 0, 0)
        resp = self.send_recv(sock, port, bad_req)
        self.assertIsNotNone(resp)
        self.assertWrongMsg(resp, prefix=bad_req[:12], status=255)

    def test_truncated_messages_are_rejected(self):
        _, sock, port = self.launch_server(row="11111", timeout=2)

        for bad_req in [
            b"",
            b"\x00",
            struct.pack("!B", MSG_JOIN),
            struct.pack("!BI", MSG_JOIN, 123)[:-1],
            struct.pack("!BII", MSG_KEEP_ALIVE, 1, 2)[:-2],
            struct.pack("!BII B".replace(" ", ""), MSG_MOVE_1, 1, 2, 3)[:-3],
        ]:
            with self.subTest(length=len(bad_req)):
                resp = self.send_recv(sock, port, bad_req)
                self.assertIsNotNone(resp)
                self.assertWrongMsg(resp, prefix=bad_req[:12], status=255)

    def test_extra_trailing_bytes_make_message_invalid(self):
        _, sock, port = self.launch_server(row="11111", timeout=2)

        bad_req = pack_client_msg(MSG_JOIN, 7) + b"\x99\x98\x97"
        resp = self.send_recv(sock, port, bad_req)
        self.assertIsNotNone(resp)
        self.assertWrongMsg(resp, prefix=bad_req[:12], status=255)

    def test_invalid_participant_or_unknown_game_is_rejected(self):
        _, sock, port = self.launch_server(row="11111", timeout=2)

        first = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 101)))
        game_id = first["game_id"]
        unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 202)))

        # Third player forges a message into an existing game.
        resp = self.send_recv(sock, port, pack_client_msg(MSG_KEEP_ALIVE, 303, game_id))
        self.assertIsNotNone(resp)
        self.assertWrongMsg(resp, status=255)

        # Known player, bogus game id.
        resp2 = self.send_recv(sock, port, pack_client_msg(MSG_KEEP_ALIVE, 101, game_id + 12345))
        self.assertIsNotNone(resp2)
        self.assertWrongMsg(resp2, status=255)

    def test_timeout_removes_waiting_game(self):
        _, sock, port = self.launch_server(row="11111", timeout=2)

        first = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 555)))
        game_id = first["game_id"]

        time.sleep(3)

        resp = self.send_recv(sock, port, pack_client_msg(MSG_KEEP_ALIVE, 555, game_id))
        self.assertIsNotNone(resp)
        self.assertWrongMsg(resp, status=255)

    def test_finished_game_is_retained_then_removed(self):
        _, sock, port = self.launch_server(row="1", timeout=2)

        first = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 10)))
        game_id = first["game_id"]
        unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 20)))

        # One-pawn board: B takes the only pawn and wins.
        win_state = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_MOVE_1, 20, game_id, 0)))
        self.assertEqual(win_state["status"], WIN_B)
        self.assertEqual(win_state["pawn_row"], b"\x00")

        # Within timeout the finished game still exists.
        still_there = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_KEEP_ALIVE, 10, game_id)))
        self.assertEqual(still_there["status"], WIN_B)

        time.sleep(3)

        gone = self.send_recv(sock, port, pack_client_msg(MSG_KEEP_ALIVE, 10, game_id))
        self.assertIsNotNone(gone)
        self.assertWrongMsg(gone, status=255)

    def test_independent_games_do_not_interfere(self):
        _, sock, port = self.launch_server(row="11111", timeout=2)

        g1 = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 1)))
        game1 = g1["game_id"]
        g1b = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 2)))
        self.assertEqual(g1b["game_id"], game1)
        self.assertEqual(g1b["status"], TURN_B)

        g2 = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_JOIN, 3)))
        game2 = g2["game_id"]
        self.assertNotEqual(game1, game2)
        self.assertEqual(g2["status"], WAITING_FOR_OPPONENT)

        # Keepalive for game1 must not mutate game2.
        s1 = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_KEEP_ALIVE, 1, game1)))
        self.assertEqual(s1["game_id"], game1)
        self.assertEqual(s1["status"], TURN_B)

        s2 = unpack_game_state(self.send_recv(sock, port, pack_client_msg(MSG_KEEP_ALIVE, 3, game2)))
        self.assertEqual(s2["game_id"], game2)
        self.assertEqual(s2["status"], WAITING_FOR_OPPONENT)


class TestKaylesClient(KaylesTestBase):
    def run_client(self, msg_str, server_port, timeout=1):
        proc = subprocess.Popen(
            [CLIENT_BIN, "-a", HOST, "-p", str(server_port), "-m", msg_str, "-t", str(timeout)],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        return proc

    def test_client_sends_correct_payloads_for_all_message_types(self):
        dummy_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        dummy_sock.bind((HOST, 0))
        _, port = dummy_sock.getsockname()
        dummy_sock.settimeout(2.0)
        self.addCleanup(dummy_sock.close)

        cases = [
            ("0/42", 5, struct.pack("!BI", MSG_JOIN, 42)),
            ("1/42/7/0", 10, struct.pack("!BII B".replace(" ", ""), MSG_MOVE_1, 42, 7, 0)),
            ("2/42/7/1", 10, struct.pack("!BII B".replace(" ", ""), MSG_MOVE_2, 42, 7, 1)),
            ("3/42/7", 9, struct.pack("!BII".replace(" ", ""), MSG_KEEP_ALIVE, 42, 7)),
            ("4/42/7", 9, struct.pack("!BII".replace(" ", ""), MSG_GIVE_UP, 42, 7)),
        ]

        for msg_str, expected_len, expected_payload in cases:
            with self.subTest(msg_str=msg_str):
                proc = self.run_client(msg_str, port, timeout=1)
                try:
                    data, addr = dummy_sock.recvfrom(1024)
                except socket.timeout:
                    proc.kill()
                    self.fail(f"client did not send payload for message {msg_str}")

                self.assertEqual(len(data), expected_len)
                self.assertEqual(data, expected_payload)

                # Send back a syntactically valid game state so the client exits cleanly.
                resp = pack_game_state(7, 42, 99, TURN_B, 4, b"\xF8")
                dummy_sock.sendto(resp, addr)

                out, err = proc.communicate(timeout=2)
                self.assertEqual(proc.returncode, 0, msg=err.decode("utf-8", errors="replace"))

    def test_client_handles_server_timeout_without_error_exit(self):
        port = free_udp_port()
        proc = self.run_client("0/999", port, timeout=1)
        out, err = proc.communicate(timeout=3)

        self.assertEqual(proc.returncode, 0)
        self.assertTrue(out or err is not None)

    def test_client_rejects_invalid_cli_arguments(self):
        bad_runs = [
            [CLIENT_BIN, "-a", HOST, "-p", "0", "-m", "0/1", "-t", "1"],
            [CLIENT_BIN, "-a", HOST, "-p", "70000", "-m", "0/1", "-t", "1"],
            [CLIENT_BIN, "-a", HOST, "-p", "12345", "-m", "not/a/message", "-t", "1"],
            [CLIENT_BIN, "-a", HOST, "-p", "12345", "-m", "0/1", "-t", "0"],
        ]
        for cmd in bad_runs:
            with self.subTest(cmd=cmd):
                proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=2)
                self.assertEqual(proc.returncode, 1)


class TestKaylesServerCliValidation(unittest.TestCase):
    def run_server(self, args):
        return subprocess.run(
            [SERVER_BIN] + args,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=2,
            )

    def test_server_rejects_invalid_rows(self):
        cases = [
            ["-r", "", "-a", HOST, "-p", "0", "-t", "2"],
            ["-r", "0", "-a", HOST, "-p", "0", "-t", "2"],
            ["-r", "10", "-a", HOST, "-p", "0", "-t", "2"],
            ["-r", "1112", "-a", HOST, "-p", "0", "-t", "2"],
            ["-r", "1" * 257, "-a", HOST, "-p", "0", "-t", "2"],
        ]
        for args in cases:
            with self.subTest(args=args):
                proc = self.run_server(args)
                self.assertEqual(proc.returncode, 1)

    def test_server_rejects_invalid_numeric_ranges(self):
        cases = [
            ["-r", "1", "-a", HOST, "-p", "-1", "-t", "2"],
            ["-r", "1", "-a", HOST, "-p", "70000", "-t", "2"],
            ["-r", "1", "-a", HOST, "-p", "0", "-t", "0"],
            ["-r", "1", "-a", HOST, "-p", "0", "-t", "100"],
        ]
        for args in cases:
            with self.subTest(args=args):
                proc = self.run_server(args)
                self.assertEqual(proc.returncode, 1)

    def test_server_rejects_bad_address(self):
        proc = self.run_server(["-r", "1", "-a", "300.300.300.300", "-p", "0", "-t", "2"])
        self.assertEqual(proc.returncode, 1)


from contextlib import closing

def free_udp_port_more():
    with closing(socket.socket(socket.AF_INET, socket.SOCK_DGRAM)) as s:
        s.bind((HOST, 0))
        return s.getsockname()[1]


def pack_game_state_more(game_id, player_a_id, player_b_id, status, max_pawn, pawn_row):
    return struct.pack('!I I I B B', game_id, player_a_id, player_b_id, status, max_pawn) + pawn_row


class TestKaylesServerMore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.isfile(SERVER_BIN):
            raise FileNotFoundError(f"{SERVER_BIN} not found.")

    def setUp(self):
        self.port = free_udp_port_more()
        self.server_proc = subprocess.Popen(
            [SERVER_BIN, '-r', '11111', '-a', HOST, '-p', str(self.port), '-t', '2'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        time.sleep(0.15)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.settimeout(0.8)

        self.addCleanup(self.sock.close)
        self.addCleanup(terminate_process, self.server_proc)

        if self.server_proc.poll() is not None:
            err = self.server_proc.stderr.read().decode('utf-8', errors='replace')
            self.fail(f"server failed to start: {err.strip()}")

    def send_recv(self, payload, sock=None):
        if sock is None:
            sock = self.sock
        sock.sendto(payload, (HOST, self.port))
        try:
            data, _ = sock.recvfrom(4096)
            return data
        except socket.timeout:
            return None

    def test_minimum_board_length_one_pawn(self):
        self.addCleanup(terminate_process, self.server_proc)
        terminate_process(self.server_proc)

        self.server_proc = subprocess.Popen(
            [SERVER_BIN, '-r', '1', '-a', HOST, '-p', str(self.port), '-t', '2'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        time.sleep(0.15)

        resp = self.send_recv(pack_client_msg(MSG_JOIN, 1))
        state = unpack_game_state(resp)
        self.assertEqual(state['max_pawn'], 0)
        self.assertEqual(state['pawn_row'], b'\x80')

    def test_maximum_board_length_256_pawns(self):
        self.addCleanup(terminate_process, self.server_proc)
        terminate_process(self.server_proc)

        row = '1' * 256
        self.server_proc = subprocess.Popen(
            [SERVER_BIN, '-r', row, '-a', HOST, '-p', str(self.port), '-t', '2'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        time.sleep(0.15)

        resp = self.send_recv(pack_client_msg(MSG_JOIN, 1))
        state = unpack_game_state(resp)
        self.assertEqual(state['max_pawn'], 255)
        self.assertEqual(len(state['pawn_row']), 32)
        self.assertEqual(state['pawn_row'][0], 0xFF)
        self.assertEqual(state['pawn_row'][-1], 0xFF)

    def test_trailing_bits_beyond_max_pawn_are_zeroed(self):
        self.addCleanup(terminate_process, self.server_proc)
        terminate_process(self.server_proc)

        self.server_proc = subprocess.Popen(
            [SERVER_BIN, '-r', '1111111', '-a', HOST, '-p', str(self.port), '-t', '2'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        time.sleep(0.15)

        resp = self.send_recv(pack_client_msg(MSG_JOIN, 1))
        state = unpack_game_state(resp)
        self.assertEqual(state['max_pawn'], 6)
        self.assertEqual(state['pawn_row'], b'\xFE')

    def test_player_id_zero_is_rejected_on_join(self):
        resp = self.send_recv(pack_client_msg(MSG_JOIN, 0))
        self.assertIsNotNone(resp)
        err = unpack_wrong_msg(resp)
        self.assertEqual(err['status'], 255)

    def test_player_id_zero_is_rejected_on_keepalive_and_move(self):
        join_a = unpack_game_state(self.send_recv(pack_client_msg(MSG_JOIN, 10)))
        game_id = join_a['game_id']
        self.send_recv(pack_client_msg(MSG_JOIN, 20))

        resp1 = self.send_recv(pack_client_msg(MSG_KEEP_ALIVE, 0, game_id))
        self.assertIsNotNone(resp1)
        self.assertEqual(unpack_wrong_msg(resp1)['status'], 255)

        resp2 = self.send_recv(pack_client_msg(MSG_MOVE_1, 0, game_id, 0))
        self.assertIsNotNone(resp2)
        self.assertEqual(unpack_wrong_msg(resp2)['status'], 255)

    def test_keepalive_from_different_source_port_is_accepted(self):
        join_a = unpack_game_state(self.send_recv(pack_client_msg(MSG_JOIN, 101)))
        game_id = join_a['game_id']
        self.send_recv(pack_client_msg(MSG_JOIN, 202))

        other_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        other_sock.bind((HOST, 0))
        other_sock.settimeout(0.8)
        self.addCleanup(other_sock.close)

        resp = self.send_recv(pack_client_msg(MSG_KEEP_ALIVE, 101, game_id), sock=other_sock)
        self.assertIsNotNone(resp)
        state = unpack_game_state(resp)
        self.assertEqual(state['game_id'], game_id)
        self.assertEqual(state['player_a_id'], 101)
        self.assertEqual(state['player_b_id'], 202)
        self.assertEqual(state['status'], TURN_B)

    def test_repeated_keepalive_does_not_change_board(self):
        join_a = unpack_game_state(self.send_recv(pack_client_msg(MSG_JOIN, 11)))
        game_id = join_a['game_id']
        self.send_recv(pack_client_msg(MSG_JOIN, 22))

        before = unpack_game_state(self.send_recv(pack_client_msg(MSG_KEEP_ALIVE, 11, game_id)))
        after = unpack_game_state(self.send_recv(pack_client_msg(MSG_KEEP_ALIVE, 22, game_id)))

        self.assertEqual(before['pawn_row'], after['pawn_row'])
        self.assertEqual(before['status'], after['status'])
        self.assertEqual(before['game_id'], after['game_id'])

    def test_new_game_after_finished_game_gets_fresh_id(self):
        terminate_process(self.server_proc)
        self.server_proc = subprocess.Popen(
            [SERVER_BIN, '-r', '1', '-a', HOST, '-p', str(self.port), '-t', '2'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        time.sleep(0.15)

        join_a = unpack_game_state(self.send_recv(pack_client_msg(MSG_JOIN, 1)))
        game1 = join_a['game_id']
        self.send_recv(pack_client_msg(MSG_JOIN, 2))

        win_state = unpack_game_state(
            self.send_recv(pack_client_msg(MSG_MOVE_1, 2, game1, 0))
        )
        self.assertEqual(win_state['status'], WIN_B)
        self.assertEqual(win_state['pawn_row'], b'\x00')

        new_game = unpack_game_state(
            self.send_recv(pack_client_msg(MSG_JOIN, 3))
        )
        self.assertNotEqual(new_game['game_id'], game1)
        self.assertEqual(new_game['player_a_id'], 3)
        self.assertEqual(new_game['player_b_id'], 0)
        self.assertEqual(new_game['status'], WAITING_FOR_OPPONENT)

    def test_finished_game_still_answers_keepalive_and_then_expires(self):
        terminate_process(self.server_proc)
        self.server_proc = subprocess.Popen(
            [SERVER_BIN, '-r', '1', '-a', HOST, '-p', str(self.port), '-t', '2'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        time.sleep(0.15)

        join_a = unpack_game_state(self.send_recv(pack_client_msg(MSG_JOIN, 10)))
        game_id = join_a['game_id']
        self.send_recv(pack_client_msg(MSG_JOIN, 20))

        win_state = unpack_game_state(self.send_recv(pack_client_msg(MSG_MOVE_1, 20, game_id, 0)))
        self.assertEqual(win_state['status'], WIN_B)
        self.assertEqual(win_state['pawn_row'], b'\x00')

        still_alive = unpack_game_state(self.send_recv(pack_client_msg(MSG_KEEP_ALIVE, 10, game_id)))
        self.assertEqual(still_alive['status'], WIN_B)

        time.sleep(3)

        expired = self.send_recv(pack_client_msg(MSG_KEEP_ALIVE, 10, game_id))
        self.assertIsNotNone(expired)
        self.assertEqual(unpack_wrong_msg(expired)['status'], 255)


class TestKaylesClientMore(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not os.path.isfile(CLIENT_BIN):
            raise FileNotFoundError(f"{CLIENT_BIN} not found.")

    def test_client_accepts_wrong_msg_reply_and_exits_zero(self):
        dummy_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        dummy_sock.bind((HOST, 0))
        _, port = dummy_sock.getsockname()
        dummy_sock.settimeout(2.0)
        self.addCleanup(dummy_sock.close)

        proc = subprocess.Popen(
            [CLIENT_BIN, '-a', HOST, '-p', str(port), '-m', '0/42', '-t', '1'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        data, addr = dummy_sock.recvfrom(1024)
        self.assertEqual(len(data), 5)

        wrong_msg = b'\x01' * 12 + b'\xff\x03'
        dummy_sock.sendto(wrong_msg, addr)

        out, err = proc.communicate(timeout=2)
        self.assertEqual(proc.returncode, 0, msg=err.decode('utf-8', errors='replace'))

    def test_client_rejects_malformed_message_strings(self):
        bad_messages = [
            '0//1',
            '1/1/2',
            '2/1/2/3/4',
            '3/1',
            '4/1',
            'x/1/2',
            '',
        ]
        for msg in bad_messages:
            with self.subTest(msg=msg):
                proc = subprocess.run(
                    [CLIENT_BIN, '-a', HOST, '-p', '12345', '-m', msg, '-t', '1'],
                    stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,
                    timeout=2
                )
                self.assertEqual(proc.returncode, 1)

    def test_client_handles_timeout_without_hanging(self):
        port = free_udp_port_more()
        proc = subprocess.Popen(
            [CLIENT_BIN, '-a', HOST, '-p', str(port), '-m', '0/999', '-t', '1'],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )
        out, err = proc.communicate(timeout=3)
        self.assertEqual(proc.returncode, 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)