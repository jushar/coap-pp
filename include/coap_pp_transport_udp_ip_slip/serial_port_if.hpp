#ifndef COAP_PP_TRANSPORT_UDP_IP_SLIP_SERIAL_PORT_IF_HPP
#define COAP_PP_TRANSPORT_UDP_IP_SLIP_SERIAL_PORT_IF_HPP

#include <cstddef>

#include "coap_pp/util/span.hpp"

namespace coap_pp {

// Platform-agnostic serial port interface used by UdpIpSlipTransport.
// Each platform provides its own concrete implementation (e.g., POSIX termios,
// STM32 HAL UART, Windows COM port).
class SerialPortIF {
 public:
  virtual ~SerialPortIF() = default;

  SerialPortIF(const SerialPortIF&) = delete;
  SerialPortIF& operator=(const SerialPortIF&) = delete;
  SerialPortIF(SerialPortIF&&) = delete;
  SerialPortIF& operator=(SerialPortIF&&) = delete;

  // Transmit all bytes synchronously. Blocks until every byte has been handed
  // to the hardware / OS send buffer.
  virtual void Write(span<const std::byte> data) = 0;

  // Receive one byte. Returns the byte value in [0, 255], or -1 on timeout /
  // no data. Must return within a bounded time (e.g., 100 ms) so that the
  // receive thread can check for a stop signal.
  virtual int ReadByte() = 0;

 protected:
  SerialPortIF() = default;
};

}  // namespace coap_pp

#endif  // COAP_PP_TRANSPORT_UDP_IP_SLIP_SERIAL_PORT_IF_HPP
