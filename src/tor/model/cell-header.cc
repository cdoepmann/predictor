#include "cell-header.h"
#include "ns3/address-utils.h"

namespace ns3 {

NS_OBJECT_ENSURE_REGISTERED (CellHeader);

CellHeader::CellHeader ()
{
  m_circId = m_cellType = m_streamId = m_length = m_cmd = 0;
}

CellHeader::~CellHeader ()
{
}

TypeId
CellHeader::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::CellHeader")
    .SetParent<Header> ()
    .AddConstructor<CellHeader> ()
  ;
  return tid;
}

TypeId
CellHeader::GetInstanceTypeId (void) const
{
  return GetTypeId ();
}

uint32_t
CellHeader::GetSerializedSize (void) const
{
  return 2 + 1 + 2 + 6 + 2 + 1;
}

void
CellHeader::Print (std::ostream &os) const
{
  os << "id=" << m_circId;
  if (m_cmd == RELAY_DATA)
    {
      os << " RELAY_DATA";
    }
  if (m_cmd == RELAY_SENDME)
    {
      os << " SENDME";
    }
}

void
CellHeader::SetCircId (uint16_t id)
{
  m_circId = id;
}

void
CellHeader::SetType (uint8_t type)
{
  m_cellType = type;
}

void
CellHeader::SetStreamId (uint16_t id)
{
  m_streamId = id;
}

void
CellHeader::SetDigest (uint8_t digest[6])
{
  memcpy (m_digest, digest, sizeof(m_digest));
}

void
CellHeader::SetLength (uint16_t len)
{
  m_length = len;
}

void
CellHeader::SetCmd (uint8_t cmd)
{
  m_cmd = cmd;
}

uint16_t
CellHeader::GetCircId (void) const
{
  return m_circId;
}

uint8_t
CellHeader::GetType (void) const
{
  return m_cellType;
}

uint16_t
CellHeader::GetStreamId (void) const
{
  return m_streamId;
}

void
CellHeader::GetDigest (uint8_t digest[6])
{
  memcpy (digest, m_digest, sizeof(m_digest));
}

uint16_t
CellHeader::GetLength (void) const
{
  return m_length;
}

uint8_t
CellHeader::GetCmd (void) const
{
  return m_cmd;
}

void
CellHeader::Serialize (Buffer::Iterator start) const
{
  Buffer::Iterator i = start;
  i.WriteU16 (m_circId);
  i.WriteU8 (m_cellType);
  i.WriteU16 (m_streamId);
  i.Write (m_digest, 6);
  i.WriteU16 (m_length);
  i.WriteU8 (m_cmd);
}

uint32_t
CellHeader::Deserialize (Buffer::Iterator start)
{
  Buffer::Iterator i = start;
  m_circId = i.ReadU16 ();
  m_cellType = i.ReadU8 ();
  m_streamId = i.ReadU16 ();
  i.Read (m_digest, 6);
  m_length = i.ReadU16 ();
  m_cmd = i.ReadU8 ();
  return GetSerializedSize ();
}


}; // namespace ns3
