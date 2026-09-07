# invoke SourceDir generated makefile for mmw_mss.per4ft
mmw_mss.per4ft: .libraries,mmw_mss.per4ft
.libraries,mmw_mss.per4ft: package/cfg/mmw_mss_per4ft.xdl
	$(MAKE) -f C:\Users\sandr\workspace_ccstheia\Out_Of_Box_1843_MSS/src/makefile.libs

clean::
	$(MAKE) -f C:\Users\sandr\workspace_ccstheia\Out_Of_Box_1843_MSS/src/makefile.libs clean

