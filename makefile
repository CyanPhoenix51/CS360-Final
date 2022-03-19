all: mftp.c mftp.c
	cc -o mftp mftp.c mftp.h
	cc -o mftpserve mftpserve.c mftp.h
clean:
	rm -f mftp mftpserve