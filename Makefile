CXX = m68k-atari-mint-g++

STRIP = m68k-atari-mint-strip

SRC_DIR := ./
OBJ_DIR := ./build
BIN_DIR := ./bin


SRC := $(wildcard $(SRC_DIR)/*.cpp) $(wildcard $(SRC_DIR)/*/*.cpp)

BIN := $(BIN_DIR)/mm_faad_mp4.prg

OBJ := $(SRC:$(SRC_DIR)/%.cpp=$(OBJ_DIR)/%.o)

CXXFLAGS := -I./ 

# CFLAGS   :=  -fomit-frame-pointer -fno-strict-aliasing -O2 -ffast-math
CFLAGS   := -m68020-60 -fomit-frame-pointer -fno-strict-aliasing -Ofast
# CFLAGS   := -m68020-60 -pg 
# LDFLAGS  := -pg
# CFLAGS   :=  -mcfv4e -fomit-frame-pointer -fno-strict-aliasing -O2 -ffast-math

# CFLAGS += -Wl,--stack,10485760

# LDFLAGS  :=

LDLIBS   := -lgem -lfaad -lopenh264 -lpthread -lyuv -lpng -lz -lm -lzita-resampler -lmp4v2

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ) | $(BIN_DIR)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) -o $@
	$(STRIP) $(BIN)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(CFLAGS) -c $< -o $@
	
$(BIN_DIR) $(OBJ_DIR):
	@mkdir -p $(@D)

clean:
	@$(RM) -rv $(BIN) $(OBJ_DIR)

-include $(OBJ:.o=.d)