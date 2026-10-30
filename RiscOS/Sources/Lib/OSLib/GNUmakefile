# Cross-compilation makefile for RISC OS Open hosted variant of OSLib library
#

COMPONENT   = OSLib

HDRS_COMPUTER = \
        ADFS        \
        CD          \
        CDFS        \
        DeviceFS    \
        DOSFS       \
        Econet      \
        FileCore    \
        Filer       \
        FilerAction \
        FileSwitch  \
        Free        \
        Freeway     \
        FSLock      \
        Joystick    \
        NetFiler    \
        NetFS       \
        NetPrint    \
        NFS         \
        OSArgs      \
        OSFile      \
        OSFind      \
        OSFSControl \
        OSGBPB      \
        OSSerialOp  \
        Parallel    \
        PipeFS      \
        RamFS       \
        ResourceFS  \
        SCSI        \
        SCSIFS      \
        ShareFS     \
        Socket      \

HDRS_CORE = \
        Buffer               \
        Cache                \
        ColourTrans          \
        Debugger             \
        DMA                  \
        FPEmulator           \
        IIC                  \
        Macros               \
        MessageTrans         \
        OS                   \
        OSByte               \
        OSHeap               \
        OSModule             \
        OSReadSysInfo        \
        OSSpriteOp           \
        OSWord               \
        Podule               \
        Portable             \
        ScreenBlanker        \
        ScreenModes          \
        ServiceInternational \
        SharedCLibrary       \
        Shell                \
        Sound                \
        Squash               \
        Territory            \

HDRS_TOOLBOX = \
        ActionButton          \
        Adjuster              \
        Button                \
        ColourDbox            \
        ColourMenu            \
        DCS                   \
        DDEUtils              \
        DisplayField          \
        DragAnObject          \
        Draggable             \
        FileDbox              \
        FileInfo              \
        FontDbox              \
        FontMenu              \
        FrontEnd              \
        Gadget                \
        Iconbar               \
        KeyboardShortcut      \
        Label                 \
        LabelledBox           \
        Menu                  \
        NumberRange           \
        OptionButton          \
        PopUp                 \
        PrintDbox             \
        ProgInfo              \
        Quit                  \
        RadioButton           \
        ResEd                 \
        SaveAs                \
        Scale                 \
        Scrollbar             \
        ScrollList            \
        Slider                \
        StringSet             \
        TextArea              \
        TextGadgets           \
        ToolAction            \
        Toolbox               \
        Window                \
        WindowSupportExternal \
        WritableField         \

HDRS_TYPES = \
        Types                \
        ColTrans32           \
        DeviceFS32           \
        DOSFS32              \
        FileCore32           \
        FileSwch32           \
        OSArgs32             \
        OSCore32             \
        OSF32                \
        OSFind32             \
        OSFSCtrl32           \
        OSGBPB32             \
        PDriver32            \
        Wimp32               \

HDRS_USER = \
        Alarm                \
        Arcweb               \
        BASICTrans           \
        ColourPicker         \
        CompressJPEG         \
        DragASprite          \
        Draw                 \
        DrawFile             \
        Filter               \
        Font                 \
        Help                 \
        Hourglass            \
        InetSuite            \
        JPEG                 \
        MakePSFont           \
        MimeMap              \
        OLE                  \
        PDriver              \
        PDumper              \
        PlugIn               \
        RemotePrinterSupport \
        Report               \
        StrongHelp           \
        Super                \
        TaskManager          \
        TaskWindow           \
        URI                  \
        URL                  \
        Wimp                 \
        WimpExtend           \
        WimpReadSysInfo      \
        WimpSpriteOp         \

OBJ_SUBDIRS = \
        ADFS                  \
        CD                    \
        CDFS                  \
        DeviceFS              \
        DOSFS                 \
        Econet                \
        FileCore              \
        Filer                 \
        FilerAction           \
        FileSwitch            \
        Free                  \
        Freeway               \
        FSLock                \
        Joystick              \
        NetFS                 \
        NetPrint              \
        NFS                   \
        OSArgs                \
        OSFile                \
        OSFind                \
        OSFSControl           \
        OSGBPB                \
        OSSerialOp            \
        Parallel              \
        PipeFS                \
        RamFS                 \
        Resolver              \
        ResourceFS            \
        SCSI                  \
        SCSIFS                \
        ShareFS               \
        Socket                \
        SysLog                \
        BlendTable            \
        Buffer                \
        Cache                 \
        ColourMap             \
        Debugger              \
        DMA                   \
        FPEmulator            \
        HAL                   \
        IIC                   \
        InverseTable          \
        MessageTrans          \
        OS                    \
        OSByte                \
        OSHeap                \
        OSModule              \
        OSReadSysInfo         \
        OSSpriteOp            \
        OSWord                \
        PCI                   \
        Podule                \
        Portable              \
        ScreenBlanker         \
        ScreenModes           \
        ServiceInternational  \
        SharedCLibrary        \
        Sound                 \
        Squash                \
        Territory             \
        ActionButton          \
        Button                \
        ColourDbox            \
        ColourMenu            \
        DCS                   \
        DDEUtils              \
        DisplayField          \
        DragAnObject          \
        Draggable             \
        FileInfo              \
        FontDbox              \
        FontMenu              \
        FrontEnd              \
        Gadget                \
        Iconbar               \
        Menu                  \
        NumberRange           \
        OptionButton          \
        PopUp                 \
        PrintDbox             \
        ProgInfo              \
        Quit                  \
        RadioButton           \
        SaveAs                \
        Scale                 \
        Scrollbar             \
        ScrollList            \
        Slider                \
        StringSet             \
        TextArea              \
        TextGadgets           \
        ToolAction            \
        Toolbox               \
        Window                \
        WindowSupportExternal \
        WritableField         \
        BASICTrans            \
        ColourPicker          \
        CompressJPEG          \
        ConvertPNG            \
        DragASprite           \
        Draw                  \
        DrawFile              \
        Filter                \
        Font                  \
        Hourglass             \
        ImageFileRender       \
        JPEG                  \
        LineEditor            \
        MimeMap               \
        PDriver               \
        PDumper               \
        Report                \
        TaskManager           \
        TaskWindow            \
        URI                   \
        URL                   \
        Wimp                  \
        WimpExtend            \
        WimpReadSysInfo       \
        WimpSpriteOp          \
        ZapFontMenu           \
        ZapRedraw             \
        Zipper                \
        ZLib                  \
        ColourTrans           \

# OSLib uses camelcase for assembler headers, so the above lists are declared in that form
#            lowercase for C headers, which can be derived by lowercasing the assembler filename
# Brute force lowercase A-Z to a-z of a string (credit:Eric Melski for this tip)
lower = $(subst A,a,$(subst B,b,$(subst C,c,$(subst D,d,$(subst E,e,$(subst F,f,$(subst G,g,$(subst H,h,$(subst I,i,$(subst J,j,$(subst K,k,$(subst L,l,$(subst M,m,$(subst N,n,$(subst O,o,$(subst P,p,$(subst Q,q,$(subst R,r,$(subst S,s,$(subst T,t,$(subst U,u,$(subst V,v,$(subst W,w,$(subst X,x,$(subst Y,y,$(subst Z,z,$1))))))))))))))))))))))))))

EXPORTING_HDRS_COMPUTER    = $(addsuffix .exphdrcom,$(call lower,${HDRS_COMPUTER}))
EXPORTING_ASMHDRS_COMPUTER = $(addsuffix .expasmcom,${HDRS_COMPUTER})
EXPORTING_HDRS_CORE        = $(addsuffix .exphdrcor,$(call lower,${HDRS_CORE}))
EXPORTING_ASMHDRS_CORE     = $(addsuffix .expasmcor,${HDRS_CORE})
EXPORTING_HDRS_TOOLBOX     = $(addsuffix .exphdrtbx,$(call lower,${HDRS_TOOLBOX}))
EXPORTING_ASMHDRS_TOOLBOX  = $(addsuffix .expasmtbx,${HDRS_TOOLBOX})
EXPORTING_HDRS_TYPES       = $(addsuffix .exphdrtyp,$(call lower,${HDRS_TYPES}))
EXPORTING_ASMHDRS_TYPES    = $(addsuffix .expasmtyp,${HDRS_TYPES})
EXPORTING_HDRS_USER        = $(addsuffix .exphdrusr,$(call lower,${HDRS_USER}))
EXPORTING_ASMHDRS_USER     = $(addsuffix .expasmusr,${HDRS_USER})
EXPORTING_HDRS             = ${EXPORTING_ASMHDRS_USER} \
                             ${EXPORTING_HDRS_USER} \
                             ${EXPORTING_ASMHDRS_TYPES} \
                             ${EXPORTING_HDRS_TYPES} \
                             ${EXPORTING_ASMHDRS_TOOLBOX} \
                             ${EXPORTING_HDRS_TOOLBOX} \
                             ${EXPORTING_ASMHDRS_CORE} \
                             ${EXPORTING_HDRS_CORE} \
                             ${EXPORTING_ASMHDRS_COMPUTER} \
                             ${EXPORTING_HDRS_COMPUTER} \

ifeq (${APCS},APCS-R)
BLOB = ../Dist/OSLib/o/${TARGET}${SUFFIX_DATA}
else
BLOB = ../Dist/OSLib/o/${TARGET}32${SUFFIX_DATA}
endif

TARGET       ?= ${COMPONENT}
LIBRARY      ?= ${TARGET}
LIBEXT       ?= a
EXPDIR       ?= ${LIBDIR}/${TARGET}

SOURCES_TO_SYMLINK = $(wildcard Dist/OSLib/*/oslib/h/*)
ASMHDRS_TO_SYMLINK = $(wildcard Dist/OSLib/*/oslib/Hdr/*)

include StdTools

ifneq (objs,$(notdir ${CURDIR}))

# Makefile invoked from same directory
# Create link farm, then execute the makefile from within it

ifeq (clean,${MAKECMDGOALS})
clean:
	@echo Cleaning...
	@rm -rf objs
	@echo ${COMPONENT}: cleaned
else

create_links_dir:
	@${MKDIR} objs

all_libs export export_hdrs export_libs links: create_links_dir
	$(foreach linksource,${SOURCES_TO_SYMLINK}, \
		$(shell \
			[ -L objs/$$(basename ${linksource}).h ] || ln -s ../${linksource} objs/$$(basename ${linksource}).h \
		) \
	)
	$(foreach linksource,${ASMHDRS_TO_SYMLINK}, \
		$(shell \
			[ -L objs/$$(basename ${linksource}).Hdr ] || ln -s ../${linksource} objs/$$(basename ${linksource}).Hdr \
		) \
	)
ifneq (links,${MAKECMDGOALS})
	@${MAKE} -C objs -f ../$(firstword ${MAKEFILE_LIST}) ${MAKECMDGOALS}
endif
endif

else

# Makefile invoked from objs subdirectory

all_libs: ${BLOB}
	@${ECHO} ${COMPONENT}: library pre-built

export: export_${PHASE}
	${NOP}

export_: export_libs export_hdrs
	${NOP}

create_exp_hdr_dirs:
	${MKDIR} ${EXPDIR}/Computer/oslib
	${MKDIR} ${EXPDIR}/Core/oslib
	${MKDIR} ${EXPDIR}/Docs
	${MKDIR} ${EXPDIR}/Toolbox/oslib
	${MKDIR} ${EXPDIR}/User/oslib

.SUFFIXES:  .exphdrcom .expasmcom .exphdrcor .expasmcor .exphdrtbx .expasmtbx .exphdrtyp .expasmtyp .exphdrusr .expasmusr .h .Hdr
.h.exphdrcom:;   ${CP} $< ${EXPDIR}/Computer/oslib
.Hdr.expasmcom:; ${CP} $< ${EXPDIR}/Computer/oslib
.h.exphdrcor:;   ${CP} $< ${EXPDIR}/Core/oslib
.Hdr.expasmcor:; ${CP} $< ${EXPDIR}/Core/oslib
.h.exphdrtbx:;   ${CP} $< ${EXPDIR}/Toolbox/oslib
.Hdr.expasmtbx:; ${CP} $< ${EXPDIR}/Toolbox/oslib
.h.exphdrtyp:;   ${CP} $< ${EXPDIR}/Core/oslib
.Hdr.expasmtyp:; ${CP} $< ${EXPDIR}/Core/oslib
.h.exphdrusr:;   ${CP} $< ${EXPDIR}/User/oslib
.Hdr.expasmusr:; ${CP} $< ${EXPDIR}/User/oslib

# These headers have moved directory
macros.exphdrcor: macros.h
	${CP} $< ${EXPDIR}/Core/oslib
Macros.expasmcor: Macros.Hdr
	${CP} $< ${EXPDIR}/Core/oslib

export_hdrs: create_exp_hdr_dirs ${EXPORTING_HDRS}
	${CP} ../VersionNum        ${EXPDIR}/LibVersion
	${CP} ../Dist/ChangeLog    ${EXPDIR}/ChangeLog
	${CP} ../Dist/Copying      ${EXPDIR}/Docs/Copying
	${CP} ../Dist/OSLib_API    ${EXPDIR}/Docs/OSLib_API
	${CP} ../Dist/OSLib_readme ${EXPDIR}/Docs/OSLib_readme
	${CP} ../Dist/Unix_Build   ${EXPDIR}/Docs/Unix_Build
	${CP} ../Dist/WideFuncts   ${EXPDIR}/Docs/WideFuncts
	@${ECHO} ${COMPONENT}: header export complete

export_libs: ${BLOB}
	${MKDIR} ${EXPDIR}
ifeq (GNU,${TOOLCHAIN})
	${RM} ${EXPDIR}/${TARGET}.${LIBEXT}
	for d in ${OBJ_SUBDIRS}; do mkdir -p l/$$d; done
	/opt/rool/bin/armlib -e ${BLOB} "*"
	objs=; \
	for d in ${OBJ_SUBDIRS}; do \
	    for f in l/$$d/*.o; do \
	        /opt/rool/bin/objdis $$f | sed \
	            -e 's/, A32bit//' \
	            -e 's/, FP3//' \
	            -e 's/\(\(ADD\|SUB\).*#.*\)#/\1/' \
	            > $${f%.o}.s; \
	        ${AS} -o $$f $${f%.o}.s; \
	        objs="$$objs $$f"; \
	    done; \
	done; \
	${AR} ${ARFLAGS} ${EXPDIR}/${TARGET}.${LIBEXT} $$objs
else
	${CP} $^ ${EXPDIR}/${TARGET}.${LIBEXT}
endif
	@${ECHO} ${COMPONENT}: library export complete

endif

# EOF
