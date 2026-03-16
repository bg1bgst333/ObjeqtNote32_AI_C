# ObjeqtNote - Makefile (MinGW-w64 / g++)
CXX      = g++
CXXFLAGS = -std=c++03 -Wall -DUNICODE -D_UNICODE -mwindows
LDFLAGS  = -lcomdlg32

TARGET = ObjeqtNote.exe
SRC    = src/main.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	del /Q $(TARGET) 2>nul || true
