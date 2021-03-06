import sys
#from time import sleep
from PyQt4 import QtCore, QtNetwork
#from PyQt4 import QtGui; # only if a GUI is desired

class saveClient(QtCore.QObject):
    def __init__(self, address, port, parent = None):
        super(saveClient, self).__init__(parent)

        self.blockSize = 0
        self.ipAddress = address
        self.portNumber = port

        #STATUS COMMAND VARIABLES
        self.STATUS_CMD = 3
        self.fps = 0
        self.framesLeft = 0
        self.numAvgs_stat = 1;

        #FRAME SAVE COMMAND VARIABLES
        self.FRSAVE_CMD = 2
        self.framesToSave = 0
        self.fname = str("")
        #self.fname = QtCore.QString("")
        self.numAvgs = 1

        #CREATING A SOCKET WITH CONNECTIONS
        self.socket = QtNetwork.QTcpSocket(self)
        self.socket.error.connect(self.printError)

    def requestFrames(self, file_name, num_frames, n_avgs=1):
        self.fname = file_name
        self.framesToSave = num_frames
        self.numAvgs = n_avgs
        self.blockSize = 0
        self.socket.abort()

        block = QtCore.QByteArray()
        commStream = QtCore.QDataStream(block, QtCore.QIODevice.WriteOnly)
        commStream.setVersion(QtCore.QDataStream.Qt_4_0)
        commStream.writeUInt16(0)
        commStream.writeUInt16(self.FRSAVE_CMD)
        commStream.writeUInt16(self.framesToSave)
        commStream.writeQString(self.fname)
        commStream.writeUInt16(self.numAvgs)
        commStream.device().seek(0)
        commStream.writeUInt16(block.count() - 2)
        self.socket.connectToHost(self.ipAddress, self.portNumber)
        self.socket.waitForConnected(10)
        self.socket.write(block)
        self.socket.waitForDisconnected()

    def requestStatus(self, avg_support = True):
        self.blockSize = 0
        self.socket.abort()

        block = QtCore.QByteArray()
        commStream = QtCore.QDataStream(block, QtCore.QIODevice.WriteOnly)
        commStream.setVersion(QtCore.QDataStream.Qt_4_0)
        commStream.writeUInt16(0)
        commStream.writeUInt16(self.STATUS_CMD)
        commStream.device().seek(0)
        commStream.writeUInt16(block.count() - 2)
        self.socket.connectToHost(self.ipAddress, self.portNumber)
        self.socket.waitForConnected(10)
        self.socket.write(block)
        self.socket.waitForReadyRead()
        inStream = QtCore.QDataStream(self.socket)
        inStream.setVersion(QtCore.QDataStream.Qt_4_0)

        if self.blockSize == 0:
            if self.socket.bytesAvailable() < 2:
                print("LVC: No Data Received...")
                return
            self.blockSize = inStream.readUInt16()
        self.framesLeft = inStream.readUInt16()
        self.fps = inStream.readUInt16()
        if avg_support:
            self.numAvgs_stat = inStream.readUInt16(); # new
        else:
            self.numAvgs_stat = 1;

        return (self.framesLeft, self.fps, self.numAvgs_stat);

    def printError(self,socket_error):
        errors = {
            QtNetwork.QTcpSocket.HostNotFoundError:
                "The host was not found. Please check the host name and "
                "port settings.",
 
            QtNetwork.QTcpSocket.ConnectionRefusedError:
                "The connection was refused by the peer. Make sure that " 
                "Live View is running, and check that the host name and port "
                "settings are correct.",
            
            QtNetwork.QTcpSocket.RemoteHostClosedError:
                None,
        }

        message = errors.get(socket_error,
            "The following error occurred: %s." % self.socket.errorString())
        if message is not None:
            print message

if __name__ == '__main__':
    #app = QtGui.QApplication(sys.argv) # not needed unless you want a GUI
    client = saveClient("127.0.0.1", 65000)
    # Save 1024 frames:
    client.requestFrames("/tmp/10_2_socket.raw", 10, 2); # returns immediately
    status = client.requestStatus(True)
    print('(Frames remaining, FPS, averages_per_frame)')
    print(status)
    # see save_helpers.py for an example on how to wait carefully
