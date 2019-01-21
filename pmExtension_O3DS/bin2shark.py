##E Type
##Format:
##EXXXXXXX UUUUUUUU
##YYYYYYYY YYYYYYYY

##Description: writes Y to X for U bytes.

PM_INJECT_ADDR = 0x10BA00
PM_SVCRUN_ADDR = 0x103150

0xE51FF004

with open("build/pmExtension.bin", "rb") as bin:
	with open("code.txt", "w+") as txt:
		bin.seek(0,2)
		payload_size = bin.tell()
		bin.seek(0)
		txt.write("Plugin Loader\n")
		txt.write("E{:07x} {:08x}\n".format(PM_INJECT_ADDR, payload_size))
		data = bin.read(8)
		l = len(data)
		while (l == 8):
			txt.write("{:02x}{:02x}{:02x}{:02x} {:02x}{:02x}{:02x}{:02x}\n".format(data[3],data[2],data[1],data[0],data[7],data[6],data[5],data[4]))
			data = bin.read(8)
			l = len(data)
		if (l == 4):
			txt.write("{:02x}{:02x}{:02x}{:02x} 00000000\n".format(data[3],data[2],data[1],data[0]))
		txt.write("E{:07x} {:08x}\n{:08x} {:08x}\n".format(PM_SVCRUN_ADDR, 8, 0xE51FF004, PM_INJECT_ADDR))