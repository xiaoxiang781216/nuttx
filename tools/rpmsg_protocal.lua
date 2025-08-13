-------------------------------------------------------------------------------
-- rpmsg_protocal.lua - Wireshark dissector for RPMSG protocol analysis
--
-- Licensed to the Apache Software Foundation (ASF) under one or more
-- contributor license agreements.  See the NOTICE file distributed with
-- this work for additional information regarding copyright ownership.
-- The ASF licenses this file to you under the Apache License, Version 2.0
-- (the "License"); you may not use this file except in compliance with
-- the License.  You may obtain a copy of the License at
--
--   http://www.apache.org/licenses/LICENSE-2.0
--
-- Unless required by applicable law or agreed to in writing, software
-- distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
-- WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
-- License for the specific language governing permissions and limitations
-- under the License.
--
-- Usage:
-- 1. Copy this file to your Wireshark plugins directory:
--    - Windows: %APPDATA%\Wireshark\plugins\
--    - Linux: ~/.local/lib/wireshark/plugins/
--    - macOS: ~/.config/wireshark/plugins/
-- 2. manual configuration
--    - First, try using only the Lua plugin. If Wireshark cannot automatically
--    - recognize packets with DLT=147, then use these two steps for manual
--    - configuration.
--    - mkdir -p ~/.config/wireshark
--    - echo 'uat:user_dlts:"User 0 (DLT=147)","rpmsg_trace","0","","0",""' >>
--    -       ~/.config/wireshark/preferences
-- 3. restart Wireshark
--    - wireshark -r rpmsg_trace.pcap -o
--    - "uat:user_dlts:\"User 0 (DLT=147)\",\"rpmsg_trace\",\"\",\"\",\"0\",\"\""
-------------------------------------------------------------------------------

-- Define the protocol
local p_rpmsg = Proto("rpmsg_trace", "RPMSG Protocol")

-- Constant definitions
local RPMSG_NAME_SIZE = 32

-- Field definitions
local f = p_rpmsg.fields
f.trans_type = ProtoField.uint32("rpmsg.trans.type", "Rdev", base.HEX)

-- RPMSG header fields (little-endian)
f.src = ProtoField.uint32("rpmsg.src", "Source", base.DEC)
f.dst = ProtoField.uint32("rpmsg.dst", "Destination", base.DEC)
f.service_type = ProtoField.uint32("rpmsg.service_type", "Service Type", base.HEX, {
    [1] = "PING",
    [2] = "NS",
    [3] = "SOCKET",
    [4] = "SOCKET",
    [5] = "TEST",
    [6] = "FS",
    [7] = "FS",
    [8] = "FS",
    [9] = "FS",
    [25] = "FS"
})
f.len = ProtoField.uint16("rpmsg.len", "Length", base.DEC)
f.flags = ProtoField.uint16("rpmsg.flags", "Flags", base.HEX)

-- Ping protocol fields
f.ping_cmd = ProtoField.uint32("rpmsg.ping.cmd", "Command", base.HEX)
f.ping_len = ProtoField.uint32("rpmsg.ping.len", "Length", base.DEC)
f.ping_cookie = ProtoField.uint64("rpmsg.ping.cookie", "Cookie", base.HEX)
f.ping_data = ProtoField.bytes("rpmsg.ping.data", "Data")

-- NS protocol fields
f.ns_name = ProtoField.string("rpmsg.ns.name", "Name", base.ASCII)
f.ns_addr = ProtoField.uint32("rpmsg.ns.addr", "Address", base.DEC)
f.ns_flags = ProtoField.uint32("rpmsg.ns.flags", "Flags", base.HEX)

-- Socket protocol fields
f.socket_cmd = ProtoField.uint32("rpmsg.socket.cmd", "Command", base.HEX)
f.socket_size = ProtoField.uint32("rpmsg.socket.size", "Size", base.DEC)
f.socket_pid = ProtoField.uint32("rpmsg.socket.pid", "PID", base.DEC)
f.socket_uid = ProtoField.uint32("rpmsg.socket.uid", "UID", base.DEC)
f.socket_gid = ProtoField.uint32("rpmsg.socket.gid", "GID", base.DEC)
f.socket_pos = ProtoField.uint32("rpmsg.socket.pos", "Position", base.DEC)
f.socket_data_len = ProtoField.uint32("rpmsg.socket.data_len", "Data Length", base.DEC)
f.socket_data = ProtoField.bytes("rpmsg.socket.data", "Data")

-- Test protocol fields
f.test_node = ProtoField.bytes("rpmsg.test.node", "List Node (node[4])")
f.test_cmd = ProtoField.uint32("rpmsg.test.cmd", "Command", base.HEX)
f.test_len = ProtoField.uint32("rpmsg.test.len", "Data Length", base.DEC)
f.test_cookie = ProtoField.uint64("rpmsg.test.cookie", "Cookie", base.HEX)
f.test_data = ProtoField.bytes("rpmsg.test.data", "Data")

-- ========== FS Protocol Fields ==========
-- Common header
f.fs_header_command = ProtoField.uint32("rpmsg.fs.header.command", "Command", base.HEX)
f.fs_header_result = ProtoField.int32("rpmsg.fs.header.result", "Result", base.DEC)
f.fs_header_cookie = ProtoField.uint64("rpmsg.fs.header.cookie", "Cookie", base.HEX)

-- Open command
f.fs_open_flags = ProtoField.int32("rpmsg.fs.open.flags", "Flags", base.DEC)
f.fs_open_mode = ProtoField.int32("rpmsg.fs.open.mode", "Mode", base.DEC)
f.fs_open_pathname = ProtoField.string("rpmsg.fs.open.pathname", "Pathname", base.ASCII)

-- Close command
f.fs_close_fd = ProtoField.int32("rpmsg.fs.close.fd", "File Descriptor", base.DEC)

-- Read/Write commands
f.fs_rw_fd = ProtoField.int32("rpmsg.fs.rw.fd", "File Descriptor", base.DEC)
f.fs_rw_count = ProtoField.uint32("rpmsg.fs.rw.count", "Count", base.DEC)
f.fs_rw_offset = ProtoField.int64("rpmsg.fs.rw.offset", "Offset", base.DEC)
f.fs_rw_buf = ProtoField.bytes("rpmsg.fs.rw.buf", "Buffer Data")

-- Fstat command - stat_priv_s structure fields
f.fs_stat_dev = ProtoField.uint32("rpmsg.fs.stat.dev", "Device ID", base.HEX)
f.fs_stat_mode = ProtoField.uint32("rpmsg.fs.stat.mode", "File Mode", base.HEX)
f.fs_stat_rdev = ProtoField.uint32("rpmsg.fs.stat.rdev", "Device ID (special)", base.HEX)
f.fs_stat_ino = ProtoField.uint16("rpmsg.fs.stat.ino", "Inode Number", base.DEC)
f.fs_stat_nlink = ProtoField.uint16("rpmsg.fs.stat.nlink", "Hard Links", base.DEC)
f.fs_stat_size = ProtoField.int64("rpmsg.fs.stat.size", "File Size", base.DEC)
f.fs_stat_atim_sec = ProtoField.int64("rpmsg.fs.stat.atim_sec", "Access Time (sec)", base.DEC)
f.fs_stat_atim_nsec = ProtoField.int64("rpmsg.fs.stat.atim_nsec", "Access Time (nsec)", base.DEC)
f.fs_stat_mtim_sec = ProtoField.int64("rpmsg.fs.stat.mtim_sec", "Modify Time (sec)", base.DEC)
f.fs_stat_mtim_nsec = ProtoField.int64("rpmsg.fs.stat.mtim_nsec", "Modify Time (nsec)", base.DEC)
f.fs_stat_ctim_sec = ProtoField.int64("rpmsg.fs.stat.ctim_sec", "Change Time (sec)", base.DEC)
f.fs_stat_ctim_nsec = ProtoField.int64("rpmsg.fs.stat.ctim_nsec", "Change Time (nsec)", base.DEC)
f.fs_stat_blocks = ProtoField.uint64("rpmsg.fs.stat.blocks", "Blocks Allocated", base.DEC)
f.fs_stat_uid = ProtoField.int16("rpmsg.fs.stat.uid", "User ID", base.DEC)
f.fs_stat_gid = ProtoField.int16("rpmsg.fs.stat.gid", "Group ID", base.DEC)
f.fs_stat_blksize = ProtoField.int16("rpmsg.fs.stat.blksize", "Block Size", base.DEC)
f.fs_stat_reserved = ProtoField.uint16("rpmsg.fs.stat.reserved", "Reserved", base.HEX)

-- Fstat command
f.fs_fstat_fd = ProtoField.int32("rpmsg.fs.fstat.fd", "File Descriptor", base.DEC)
f.fs_fstat_pathname = ProtoField.string("rpmsg.fs.fstat.pathname", "Pathname", base.ASCII)

-- Command value definitions (example values - adjust according to your implementation)
local FS_CMD = {
    OPEN = 1,
    CLOSE = 2,
    READ = 3,
    WRITE = 4,
    FSTAT = 20,
    -- Add other commands as needed
}

-- Dissector function
function p_rpmsg.dissector(buf, pkt, root)
    local offset = 0
    local buf_len = buf:len()

    -- 1. Parse transport layer
    if buf_len < offset + 4 then return end
    local trans_tree = root:add(p_rpmsg, buf(offset, 4), "Transport Layer")
    trans_tree:add(f.trans_type, buf(offset, 4))
    offset = offset + 4

    -- 2. Parse RPMSG header (16 bytes)
    if buf_len < offset + 16 then return end
    local header_tree = root:add(p_rpmsg, buf(offset, 16), "Header Layer")
    -- parse src
    local src_val = buf(offset, 4):le_uint()
    header_tree:add_le(f.src, buf(offset, 4))
    pkt.cols.src = tostring(src_val)
    offset = offset + 4

    -- parse dst
    local dst_val = buf(offset, 4):le_uint()
    header_tree:add_le(f.dst, buf(offset, 4))
    pkt.cols.dst = tostring(dst_val)
    offset = offset + 4

    -- parse service_type
    local service_type = buf(offset, 4):le_uint()
    local service_type_str = ""
    if service_type == 1 then service_type_str = "PING"
    elseif service_type == 2 then service_type_str = "NS"
    elseif service_type == 3 or service_type == 4 then service_type_str = "SOCKET"
    elseif service_type == 5 then service_type_str = "TEST"
    elseif service_type == 6 or service_type == 7 or service_type == 8 or service_type == 9 or service_type == 25 then
        service_type_str = "FS"
    end

    local service_type_item = header_tree:add_le(f.service_type, buf(offset, 4))
    service_type_item:set_text("Service Type: " .. service_type_str)
    offset = offset + 4

    header_tree:add_le(f.len, buf(offset, 2)); offset = offset + 2
    header_tree:add_le(f.flags, buf(offset, 2)); offset = offset + 2

    -- 3. Parse payload based on service_type field
    if service_type == 1 then  -- PING
        pkt.cols.protocol = "PING"
        if buf_len < offset + 16 then return end
        local ping_tree = root:add(p_rpmsg, buf(offset), "Ping Protocol")
        ping_tree:add_le(f.ping_cmd, buf(offset, 4)); offset = offset + 4
        ping_tree:add_le(f.ping_len, buf(offset, 4)); offset = offset + 4
        ping_tree:add_le(f.ping_cookie, buf(offset, 8)); offset = offset + 8

        local data_len = buf_len - offset
        if data_len > 0 then
            ping_tree:add(f.ping_data, buf(offset, data_len))
        end

    elseif service_type == 2 then  -- NS
        pkt.cols.protocol = "NS"
        if buf_len < offset + RPMSG_NAME_SIZE + 8 then return end
        local ns_tree = root:add(p_rpmsg, buf(offset), "NS Protocol")
        ns_tree:add(f.ns_name, buf(offset, RPMSG_NAME_SIZE)); offset = offset + RPMSG_NAME_SIZE
        ns_tree:add_le(f.ns_addr, buf(offset, 4)); offset = offset + 4
        ns_tree:add_le(f.ns_flags, buf(offset, 4))

    elseif service_type == 3 then  -- SOCKET SYNC
        pkt.cols.protocol = "SOCKET_SYNC"
        if buf_len < offset + 20 then return end
        local socket_tree = root:add(p_rpmsg, buf(offset), "Socket Layer")
        socket_tree:add_le(f.socket_cmd, buf(offset, 4)); offset = offset + 4
        local sync_tree = socket_tree:add(p_rpmsg, buf(offset), "sync Protocol")
        sync_tree:add_le(f.socket_size, buf(offset, 4)); offset = offset + 4
        sync_tree:add_le(f.socket_pid, buf(offset, 4)); offset = offset + 4
        sync_tree:add_le(f.socket_uid, buf(offset, 4)); offset = offset + 4
        sync_tree:add_le(f.socket_gid, buf(offset, 4))

    elseif service_type == 4 then  -- SOCKET DATA
        pkt.cols.protocol = "SOCKET_DATA"
        if buf_len < offset + 12 then return end
        local socket_tree = root:add(p_rpmsg, buf(offset), "Socket Layer")
        socket_tree:add_le(f.socket_cmd, buf(offset, 4)); offset = offset + 4
        local data_tree = socket_tree:add(p_rpmsg, buf(offset), "data Protocol")
        data_tree:add_le(f.socket_pos, buf(offset, 4)); offset = offset + 4
        data_tree:add_le(f.socket_data_len, buf(offset, 4)); offset = offset + 4

        local data_len = buf_len - offset
        if data_len > 0 then
            data_tree:add(f.socket_data, buf(offset, data_len))
        end

    elseif service_type == 5 then  -- TEST
        pkt.cols.protocol = "TEST"
        -- RPMSG_TEST: parse node[4] + cmd + len + data
        if buf:len() >= offset + 24 then  -- at least node[4](16) + cmd(4) + len(4)
            local test_tree = root:add(p_rpmsg, buf(offset, buf:len() - offset), "Test Payload")

            -- parse node[4]
            local node_bytes = buf(offset, 16)
            test_tree:add(f.test_node, node_bytes)
            offset = offset + 16

            -- parse cmd
            test_tree:add_le(f.test_cmd, buf(offset, 4)); offset = offset + 4

            -- parse data len
            local data_len = buf(offset, 4):le_uint()
            test_tree:add_le(f.test_len, buf(offset, 4)); offset = offset + 4

            -- parse cookie
            test_tree:add_le(f.test_cookie, buf(offset, 8)); offset = offset + 8

            -- parse data
            if buf:len() >= offset + data_len then
                test_tree:add_le(f.test_data, buf(offset, data_len))
                offset = offset + data_len
            else
                test_tree:append_text("Truncated data")
            end
        else
            root:add(p_rpmsg, buf(offset), "Malformed Test Message")
        end

    -- ========== FS SERVICE DISSECTION ==========
    elseif service_type == 6 or service_type == 7 or service_type == 8 or service_type == 9 or service_type == 25 then
        pkt.cols.protocol = "FS"

        -- Parse common FS header
        if buf_len < offset + 16 then  -- command(4) + result(4) + cookie(8) = 16 bytes
            root:add(p_rpmsg, buf(offset), "Incomplete FS Header")
            return
        end

        local fs_tree = root:add(p_rpmsg, buf(offset), "FS Protocol")
        local header_tree = fs_tree:add(p_rpmsg, buf(offset, 16), "FS Header")

        -- Parse command field
        local command = buf(offset, 4):le_uint()
        header_tree:add_le(f.fs_header_command, buf(offset, 4))
        offset = offset + 4

        -- Parse result field
        header_tree:add_le(f.fs_header_result, buf(offset, 4))
        offset = offset + 4

        -- Parse cookie field
        header_tree:add_le(f.fs_header_cookie, buf(offset, 8))
        offset = offset + 8

        -- Parse specific command payload
        if command == FS_CMD.OPEN then
            fs_tree:append_text(" (OPEN)")

            if buf_len >= offset + 8 then  -- flags(4) + mode(4)
                local open_tree = fs_tree:add(p_rpmsg, buf(offset), "Open Command")

                -- Parse flags
                open_tree:add_le(f.fs_open_flags, buf(offset, 4))
                offset = offset + 4

                -- Parse mode
                open_tree:add_le(f.fs_open_mode, buf(offset, 4))
                offset = offset + 4

                -- Parse pathname (variable length, null-terminated string)
                local pathname_end = offset
                while pathname_end < buf_len and buf(pathname_end, 1):uint() ~= 0 do
                    pathname_end = pathname_end + 1
                end

                if pathname_end < buf_len then
                    local pathname_len = pathname_end - offset
                    open_tree:add(f.fs_open_pathname, buf(offset, pathname_len))
                    offset = pathname_end + 1  -- Skip the null terminator
                else
                    open_tree:append_text("Malformed pathname (missing null terminator)")
                end
            else
                fs_tree:append_text("Incomplete OPEN command")
            end

        elseif command == FS_CMD.CLOSE then
            fs_tree:append_text(" (CLOSE)")

            if buf_len >= offset + 4 then  -- fd(4)
                local close_tree = fs_tree:add(p_rpmsg, buf(offset), "Close Command")
                close_tree:add_le(f.fs_close_fd, buf(offset, 4))
                offset = offset + 4
            else
                fs_tree:append_text("Incomplete CLOSE command")
            end

        elseif command == FS_CMD.READ or command == FS_CMD.WRITE then
            if command == FS_CMD.READ then
                fs_tree:append_text(" (READ)")
            else
                fs_tree:append_text(" (WRITE)")
            end

            if buf_len >= offset + 16 then  -- fd(4) + count(4) + offset(8) = 16 bytes
                local rw_tree = fs_tree:add(p_rpmsg, buf(offset), "Read/Write Command")

                -- Parse file descriptor
                rw_tree:add_le(f.fs_rw_fd, buf(offset, 4))
                offset = offset + 4

                -- Parse count
                local count = buf(offset, 4):le_uint()
                rw_tree:add_le(f.fs_rw_count, buf(offset, 4))
                offset = offset + 4

                -- Parse offset
                rw_tree:add_le(f.fs_rw_offset, buf(offset, 8))
                offset = offset + 8

                -- Parse data buffer (if present)
                if count > 0 and buf_len >= offset + count then
                    rw_tree:add(f.fs_rw_buf, buf(offset, count))
                    offset = offset + count
                elseif count > 0 then
                    rw_tree:append_text("Incomplete data buffer")
                end
            else
                fs_tree:append_text("Incomplete READ/WRITE command")
            end

        elseif command == FS_CMD.FSTAT then
            fs_tree:append_text(" (FSTAT)")

            -- FSTAT command structure: header(16) + stat_buf(96) + union(4 or variable length)
            local STAT_BUF_SIZE = 96  -- Calculated based on rpmsgfs_stat_priv_s structure

            if buf_len >= offset + STAT_BUF_SIZE then  -- stat_buf + at least 4 bytes for union
                local fstat_tree = fs_tree:add(p_rpmsg, buf(offset), "Fstat Command")

                -- Parse stat buffer (rpmsgfs_stat_priv_s)
                local stat_tree = fstat_tree:add(p_rpmsg, buf(offset, STAT_BUF_SIZE), "Stat Buffer")
                stat_tree:add_le(f.fs_stat_dev, buf(offset, 4)); offset = offset + 4
                stat_tree:add_le(f.fs_stat_mode, buf(offset, 4)); offset = offset + 4
                stat_tree:add_le(f.fs_stat_rdev, buf(offset, 4)); offset = offset + 4
                stat_tree:add_le(f.fs_stat_ino, buf(offset, 2)); offset = offset + 2
                stat_tree:add_le(f.fs_stat_nlink, buf(offset, 2)); offset = offset + 2
                stat_tree:add_le(f.fs_stat_size, buf(offset, 8)); offset = offset + 8
                stat_tree:add_le(f.fs_stat_atim_sec, buf(offset, 8)); offset = offset + 8
                stat_tree:add_le(f.fs_stat_atim_nsec, buf(offset, 8)); offset = offset + 8
                stat_tree:add_le(f.fs_stat_mtim_sec, buf(offset, 8)); offset = offset + 8
                stat_tree:add_le(f.fs_stat_mtim_nsec, buf(offset, 8)); offset = offset + 8
                stat_tree:add_le(f.fs_stat_ctim_sec, buf(offset, 8)); offset = offset + 8
                stat_tree:add_le(f.fs_stat_ctim_nsec, buf(offset, 8)); offset = offset + 8
                stat_tree:add_le(f.fs_stat_blocks, buf(offset, 8)); offset = offset + 8
                stat_tree:add_le(f.fs_stat_uid, buf(offset, 2)); offset = offset + 2
                stat_tree:add_le(f.fs_stat_gid, buf(offset, 2)); offset = offset + 2
                stat_tree:add_le(f.fs_stat_blksize, buf(offset, 2)); offset = offset + 2
                stat_tree:add_le(f.fs_stat_reserved, buf(offset, 2)); offset = offset + 2

                -- Parse union: either fd (4 bytes) or pathname (variable length)
                local union_tree = fstat_tree:add(p_rpmsg, buf(offset), "Union (fd or pathname)")

                -- Need to determine whether it's using fd or pathname based on implementation
                -- Simple approach: check if first byte is a digit or printable character
                local first_byte = buf(offset, 1):uint()

                if first_byte == 0 or (first_byte >= 0x30 and first_byte <= 0x39) then
                    -- Possibly fd (0 or number), or empty path
                    if buf_len >= offset + 4 then
                        local fd_val = buf(offset, 4):le_int()
                        if fd_val >= 0 and fd_val <= 1024 then  -- Reasonable fd range
                            union_tree:add_le(f.fs_fstat_fd, buf(offset, 4))
                            union_tree:append_text(" (File Descriptor)")
                            offset = offset + 4
                        else
                            -- Possibly pathname
                            local pathname_end = offset
                            while pathname_end < buf_len and buf(pathname_end, 1):uint() ~= 0 do
                                pathname_end = pathname_end + 1
                            end

                            if pathname_end < buf_len then
                                local pathname_len = pathname_end - offset
                                union_tree:add(f.fs_fstat_pathname, buf(offset, pathname_len))
                                union_tree:append_text(" (Pathname)")
                                offset = pathname_end + 1
                            else
                                union_tree:append_text("Unknown union content")
                                offset = buf_len  -- Move to end
                            end
                        end
                    end
                else
                    -- Possibly pathname
                    local pathname_end = offset
                    while pathname_end < buf_len and buf(pathname_end, 1):uint() ~= 0 do
                        pathname_end = pathname_end + 1
                    end

                    if pathname_end < buf_len then
                        local pathname_len = pathname_end - offset
                        union_tree:add(f.fs_fstat_pathname, buf(offset, pathname_len))
                        union_tree:append_text(" (Pathname)")
                        offset = pathname_end + 1
                    else
                        union_tree:append_text("Unknown union content")
                        offset = buf_len  -- Move to end
                    end
                end

                -- Display remaining data (if any)
                local remaining = buf_len - offset
                if remaining > 0 then
                    fstat_tree:add(p_rpmsg, buf(offset, remaining), "Extra Data (" .. remaining .. " bytes)")
                end

            else
                fs_tree:append_text("Incomplete FSTAT command (need at least " .. (STAT_BUF_SIZE + 4) .. " bytes, got " .. (buf_len - offset) .. ")")
            end

        else
            fs_tree:append_text(" (Unknown Command: " .. command .. ")")

            -- Display remaining bytes for unknown commands
            local remaining = buf_len - offset
            if remaining > 0 then
                fs_tree:add(p_rpmsg, buf(offset, remaining), "Unknown Data (" .. remaining .. " bytes)")
            end
        end
    end
end

-- Register the protocol
local function register_proto()
    local custom_table = DissectorTable.get("wtap_encap")
    custom_table:add(147, p_rpmsg)
end

-- Explicitly register (to handle loading timing issues)
register_proto()