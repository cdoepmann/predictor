#include "cell-header.h"
#include "ns3/address-utils.h"

namespace ns3 {

	NS_OBJECT_ENSURE_REGISTERED (CellHeader);

	CellHeader::CellHeader (){
	  circId = cellType = streamId = length = cmd = 0;
	}

	CellHeader::~CellHeader () {}
	TypeId CellHeader::GetTypeId (void) {
	  static TypeId tid = TypeId ("ns3::CellHeader")
	    .SetParent<Header> ()
	    .AddConstructor<CellHeader> ()
	    ;
	  return tid;
	}

	TypeId CellHeader::GetInstanceTypeId (void) const { return GetTypeId (); }
	uint32_t CellHeader::GetSerializedSize (void) const { return 2+1+2+6+2+1; }

	void CellHeader::Print (std::ostream &os) const {
		os << "id=" << circId;
		if (cmd == RELAY_DATA) os << " RELAY_DATA";
		if (cmd == RELAY_SENDME) os << " SENDME";
	}

	void CellHeader::SetCircId(uint16_t id){ circId = id; }
	void CellHeader::SetType (uint8_t cmd){ cellType = cmd; }
	void CellHeader::SetStreamId (uint16_t id){ streamId = id; }
	void CellHeader::SetDigest (uint8_t _digest[6]){ memcpy(digest, _digest, sizeof(digest)); }
	void CellHeader::SetLength (uint16_t len){ length = len; }
	void CellHeader::SetCmd (uint8_t _cmd){ cmd = _cmd; }

	uint16_t CellHeader::GetCircId(void) const{ return circId; }
	uint8_t CellHeader::GetType (void) const { return cellType; }
	uint16_t CellHeader::GetStreamId (void) const { return streamId; }
	void CellHeader::GetDigest (uint8_t _digest[6]) { memcpy(_digest, digest, sizeof(digest)); }
	uint16_t CellHeader::GetLength (void) const { return length; }
	uint8_t CellHeader::GetCmd (void) const { return cmd; }

	void CellHeader::Serialize (Buffer::Iterator start) const {
	  Buffer::Iterator i = start;
	  i.WriteU16 (circId);
	  i.WriteU8 (cellType);
	  i.WriteU16 (streamId);
	  i.Write(digest, 6);
	  i.WriteU16 (length);
	  i.WriteU8 (cmd);
	}

	uint32_t CellHeader::Deserialize (Buffer::Iterator start) {
	  Buffer::Iterator i = start;
	  circId = i.ReadU16();
	  cellType = i.ReadU8();
	  streamId = i.ReadU16();
	  i.Read(digest, 6);
	  length = i.ReadU16();
	  cmd = i.ReadU8();
	  return GetSerializedSize ();
	}


}; // namespace ns3
