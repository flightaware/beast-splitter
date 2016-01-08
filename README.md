# beast-splitter

This is a helper utility for the Mode-S Beast.

The Mode-S Beast provides a single data stream over a (USB) serial port.
If you have more than one thing that wants to read that data stream, you need
something to redistribute the data. This is what beast-splitter does.

## Input side

beast-splitter knows how to talk to a Mode-S Beast over a USB serial port.
The Debian package includes udev rules that should make the Beast's serial
port available as /dev/beast. If the Beast is actually somewhere else, the
--serial command line option lets you say where.

beast-splitter will try to automatically determine the baud rate that the Beast
is running at. This may take a few seconds. To explicitly set the baud rate,
there is a --fixed-baud command line option.

beast-splitter will, by default, autodetect the capabilities of the Beast and
allow clients to request any of the message-filtering options. To force
particular options on or off on the actual Beast itself, regardless of what
clients asked for, use the --force command-line option (see below for how to
specify settings). The "binary format" and "hardware handshake" options are
always used, you cannot override them with --force.

## Output side

beast-splitter provides data to network clients over TCP, by accepting
connections on a TCP port or by making outgoing connections to a given address.

To set up a listening port, specify --listen with the port number to listen on. 
You may optionally provide an interface address to bind to; if no address is
given, all addresses are bound.

To set up an outgoing connnection, specify --connect with a host and port.
beast-splitter will try to reestablish the connection if it is lost.

Both --listen and --connect accept a settings option (see below) that provides
the initial settings for new connections. After connecting, clients can
request different settings by the Beast input commands (0x1A '1' 'c', etc -
see the Beast wiki).

## Output filtering and translation

Each client can have different settings for output format and the types of
messages it wants to receive. For example, one client might want Beast binary
format, with only DF11/17/18 provided; while a second client might want all
messages in AVR format.

beast-splitter allows this. For formats other than Beast binary, and for cases
where the requested timestamp format (GPS or 12MHz) does not match that being
provided by the Beast itself, beast-splitter will translate as needed.

For cases where clients have requested different filtering options,
beast-splitter will ask the real Beast for the most general set of messages
needed and then perform per-client filtering of the messages before forwarding
them on.

## Settings

The --listen, --connect, and --force options take a "settings string" which is
a list of letters that indicates which settings to turn on or off. These
settings mostly correspond to DIP switch settings that can be set on the Beast.
Generally, uppercase means "turn on" and lowercase means "turn off". If a
setting is omitted, this is a "don't care" setting where beast-splitter will
not override that setting either way.

The available settings are:

 * B: Use Beast-Classic mode (see below)
 * R: Use Radarcape mode (see below)
 * c/C: Use AVR format / use binary format
 * d/D: no special filtering / send only DF11/17/18 messages
 * e/E: normal AVR format / AVR with timestamps format (only in AVR mode)
 * f/F: normal CRC checks / disable CRC checks
 * g/G: 12MHz timestamps / GPS timestamps (only in Radarcape mode)
 * h/H: no flow control / flow control enabled (ignored!)
 * i/I: FEC enabled / FEC disabled
 * j/J: Mode A/C disabled / Mode A/C enabled
 * k/K: no special filtering / do not send DF0/4/5 (only in Beast-Classic mode)

The h/H setting is understood but ignored; hardware flow control is always
used.

The B setting turns on Beast-Classic mode. In this mode, Radarcape status
messages are not forwarded, 12MHz timestamps are always used, the DF0/4/5
filtering option is enabled, and a request _from a client_ to set the 'g' or
'G' option is interpreted as setting the DF0/4/5 filtering options. You should
set this for connections where the client expects to talk to a Beast-Classic.

The R setting turns on Radarcape mode. In this mode, Radarcape status
messages are forwarded, the DF0/4/5 filtering option is not available,
and a request _from a client_ to set the 'g' or 'G' option is interpreted as
setting the 12MHZ/GPS timestamp options. You should set this for connections
where the client expects to talk to a Radarcape.

## Just give me an example

```
$ beast-splitter --serial /dev/beast --listen 30005:R --connect localhost:30104:R
```

This will:

 * Connect to a Beast at /dev/beast with autodetection
 * Accept TCP connections on port 30005 and send Radarcape-format data
   to clients that connect.
 * Establish (and maintain) a connection to localhost, port 30104, and
   send Radarcape-format data there.
