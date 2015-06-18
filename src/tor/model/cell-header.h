#ifndef Cell_HEADER_H
#define Cell_HEADER_H

#include <stdint.h>
#include <string>
#include "ns3/header.h"
#include "ns3/ipv4-address.h"

namespace ns3 {

//used cell types
        #define RELAY 3
        #define RELAY_DATA 2
        #define RELAY_SENDME 5

class CellHeader : public Header
{
public:
  CellHeader ();
  ~CellHeader ();

  void SetCircId (uint16_t);
  void SetType (uint8_t);
  void SetStreamId (uint16_t);
  void SetDigest (uint8_t digest[6]);
  void SetLength (uint16_t);
  void SetCmd (uint8_t);

  uint16_t GetCircId (void) const;
  uint8_t GetType (void) const;
  uint16_t GetStreamId (void) const;
  void GetDigest (uint8_t digest[6]);
  uint16_t GetLength (void) const;
  uint8_t GetCmd (void) const;

  static TypeId GetTypeId (void);
  virtual TypeId GetInstanceTypeId (void) const;
  virtual void Print (std::ostream &os) const;
  virtual uint32_t GetSerializedSize (void) const;
  virtual void Serialize (Buffer::Iterator start) const;
  virtual uint32_t Deserialize (Buffer::Iterator start);

private:
  uint16_t m_circId;
  uint8_t m_cellType;
  uint16_t m_streamId;
  uint8_t m_digest[6];
  uint16_t m_length;
  uint8_t m_cmd;
};

} // namespace ns3

#endif /* Cell_HEADER */
