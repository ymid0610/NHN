using System;
using System.Collections.Generic;
using System.IO;
using System.Text;

namespace NHN.Network
{
    internal sealed class PacketWriter
    {
        private readonly MemoryStream stream = new MemoryStream(256);
        private readonly BinaryWriter writer;

        public PacketWriter(ServerPacketId packetId)
        {
            writer = new BinaryWriter(stream, Encoding.UTF8);
            Write((ushort)0);
            Write((ushort)packetId);
        }

        public void Write(byte value)
        {
            writer.Write(value);
        }

        public void Write(bool value)
        {
            writer.Write((byte)(value ? 1 : 0));
        }

        public void Write(ushort value)
        {
            writer.Write(value);
        }

        public void Write(uint value)
        {
            writer.Write(value);
        }

        public void Write(ulong value)
        {
            writer.Write(value);
        }

        public void Write(ServerGameMode value)
        {
            Write((byte)value);
        }

        public void Write(ServerMatchConfig config)
        {
            Write(config.Rounds);
            Write(config.PaperSizeMin);
            Write(config.PaperSizeMax);
            Write(config.AmmoPerWave);
            Write(config.WaveLimit);
            Write(config.ItemMask);
        }

        public void Write(string value)
        {
            if (string.IsNullOrEmpty(value))
            {
                Write((ushort)0);
                return;
            }

            byte[] bytes = Encoding.UTF8.GetBytes(value);
            int length = Math.Min(bytes.Length, ushort.MaxValue);
            Write((ushort)length);
            writer.Write(bytes, 0, length);
        }

        public byte[] ToArray()
        {
            writer.Flush();
            byte[] bytes = stream.ToArray();
            if (bytes.Length > ushort.MaxValue)
            {
                throw new InvalidOperationException("Packet is larger than the server frame limit.");
            }

            ushort size = (ushort)bytes.Length;
            bytes[0] = (byte)(size & 0xFF);
            bytes[1] = (byte)((size >> 8) & 0xFF);
            return bytes;
        }
    }

    internal sealed class PacketReader
    {
        private readonly byte[] buffer;
        private int position;

        public PacketReader(byte[] frame)
        {
            buffer = frame;
            position = 4;
        }

        public byte ReadByte()
        {
            Ensure(1);
            return buffer[position++];
        }

        public bool ReadBool()
        {
            return ReadByte() != 0;
        }

        public ushort ReadUInt16()
        {
            Ensure(2);
            ushort value = BitConverter.ToUInt16(buffer, position);
            position += 2;
            return value;
        }

        public uint ReadUInt32()
        {
            Ensure(4);
            uint value = BitConverter.ToUInt32(buffer, position);
            position += 4;
            return value;
        }

        public ulong ReadUInt64()
        {
            Ensure(8);
            ulong value = BitConverter.ToUInt64(buffer, position);
            position += 8;
            return value;
        }

        public string ReadString()
        {
            ushort length = ReadUInt16();
            if (length == 0)
            {
                return string.Empty;
            }

            Ensure(length);
            string value = Encoding.UTF8.GetString(buffer, position, length);
            position += length;
            return value;
        }

        public List<T> ReadList<T>(Func<PacketReader, T> reader)
        {
            ushort count = ReadUInt16();
            List<T> values = new List<T>(count);
            for (int i = 0; i < count; i++)
            {
                values.Add(reader(this));
            }

            return values;
        }

        public ServerRoomMember ReadRoomMember()
        {
            return new ServerRoomMember
            {
                SessionId = ReadUInt64(),
                Nickname = ReadString(),
                Slot = ReadByte(),
                IsHost = ReadBool(),
                IsReady = ReadBool()
            };
        }

        public ServerRoomSummary ReadRoomSummary()
        {
            return new ServerRoomSummary
            {
                RoomId = ReadUInt64(),
                Name = ReadString(),
                Mode = (ServerGameMode)ReadByte(),
                State = (ServerRoomState)ReadByte(),
                MemberCount = ReadByte(),
                Capacity = ReadByte(),
                HasPassword = ReadBool(),
                HostNickname = ReadString()
            };
        }

        public ServerRoomDetail ReadRoomDetail()
        {
            return new ServerRoomDetail
            {
                RoomId = ReadUInt64(),
                Name = ReadString(),
                Mode = (ServerGameMode)ReadByte(),
                State = (ServerRoomState)ReadByte(),
                Capacity = ReadByte(),
                HasPassword = ReadBool(),
                HostSessionId = ReadUInt64(),
                Members = ReadList(reader => reader.ReadRoomMember())
            };
        }

        public ServerMatchConfig ReadMatchConfig()
        {
            return new ServerMatchConfig
            {
                Rounds = ReadByte(),
                PaperSizeMin = ReadByte(),
                PaperSizeMax = ReadByte(),
                AmmoPerWave = ReadByte(),
                WaveLimit = ReadByte(),
                ItemMask = ReadUInt32()
            };
        }

        private void Ensure(int byteCount)
        {
            if (byteCount < 0 || position + byteCount > buffer.Length)
            {
                throw new EndOfStreamException("Malformed packet body.");
            }
        }
    }
}
