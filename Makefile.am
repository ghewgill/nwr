CXXFLAGS = -g -Wall -Werror
INCLUDES = @SIGC_CFLAGS@
LDADD = @SIGC_LIBS@

bin_PROGRAMS = rec play squelch encode decode plot log streamer splitter monitor

rec_SOURCES = rec.cpp

play_SOURCES = play.cpp

squelch_SOURCES = squelch.cpp

encode_SOURCES = encode.cpp

decode_SOURCES = decode.cpp eas_demod.cpp eas_decode.cpp

plot_SOURCES = plot.cpp

log_SOURCES = log.cpp

streamer_SOURCES = streamer.cpp

splitter_SOURCES = splitter.cpp

monitor_SOURCES = monitor.cpp eas_demod.cpp eas_decode.cpp
