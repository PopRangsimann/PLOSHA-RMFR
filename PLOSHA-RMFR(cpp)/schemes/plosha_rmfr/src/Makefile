CXX = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -pthread
LIBS = -lssl -lcrypto -lpthread

CRYPTO_DIR = ../../../src/crypto

SRCS = main.cpp \
       des_engine.cpp \
       dataset_loader.cpp \
       crypto_wrapper.cpp \
       sensor.cpp \
       fog_node.cpp \
       ewma_predictor.cpp \
       plosha.cpp \
       rmfr.cpp \
       aflto.cpp \
       metrics.cpp \
       $(CRYPTO_DIR)/paillier.cpp \
       $(CRYPTO_DIR)/modified_ecdsa.cpp

OBJS = $(SRCS:.cpp=.o)
TARGET = plosha_rmfr

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LIBS)

clean:
	rm -f $(TARGET) *.o
