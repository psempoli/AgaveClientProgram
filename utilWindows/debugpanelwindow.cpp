/*********************************************************************************
**
** Copyright (c) 2017 The University of Notre Dame
** Copyright (c) 2017 The Regents of the University of California
**
** Redistribution and use in source and binary forms, with or without modification,
** are permitted provided that the following conditions are met:
**
** 1. Redistributions of source code must retain the above copyright notice, this
** list of conditions and the following disclaimer.
**
** 2. Redistributions in binary form must reproduce the above copyright notice, this
** list of conditions and the following disclaimer in the documentation and/or other
** materials provided with the distribution.
**
** 3. Neither the name of the copyright holder nor the names of its contributors may
** be used to endorse or promote products derived from this software without specific
** prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
** EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
** SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
** TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
** BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
** CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
** IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
** SUCH DAMAGE.
**
***********************************************************************************/

// Contributors:
// Written by Peter Sempolinski, for the Natural Hazard Modeling Laboratory, director: Ahsan Kareem, at Notre Dame

#include "debugpanelwindow.h"
#include "ui_debugpanelwindow.h"

#include "vwtinterfacedriver.h"
#include "../AgaveClientInterface/remotedatainterface.h"
#include "../AgaveClientInterface/filemetadata.h"

#include "remoteFileOps/filetreenode.h"
#include "remoteFileOps/remotefiletree.h"
#include "remoteFileOps/joboperator.h"

#include "visualUtils/decompresswrapper.h"

DebugPanelWindow::DebugPanelWindow(RemoteDataInterface *newDataLink, QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::DebugPanelWindow)
{
    ui->setupUi(this);

    dataLink = newDataLink;

    fileTreeData = new RemoteFileTree(dataLink, ui->remoteFileView, ui->selectedFileInfo, this);
    remoteJobLister = new JobOperator(dataLink, ui->longTaskView,this);

    QObject::connect(fileTreeData, SIGNAL(newFileSelected(FileMetaData*)),
                     this, SLOT(selectedFileChanged(FileMetaData *)));
}

DebugPanelWindow::~DebugPanelWindow()
{
    delete ui;
}

void DebugPanelWindow::startAndShow()
{
    selectedFullPath = "/";
    fileTreeData->resetFileData();

    ui->agaveAppList->setModel(&taskListModel);
    ui->agaveAppList->setEditTriggers(QAbstractItemView::NoEditTriggers);

    this->show();
}

void DebugPanelWindow::selectedFileChanged(FileMetaData * newFileData)
{
    selectedFullPath = newFileData->getFullPath();
}

void DebugPanelWindow::agaveAppSelected(QModelIndex clickedItem)
{
    selectedAgaveApp = taskListModel.itemFromIndex(clickedItem)->text();
}

void DebugPanelWindow::setTestVisual()
{
    conditionalPurge(&pointData);
    conditionalPurge(&faceData);
    conditionalPurge(&ownerData);
    ui->openGLcfdWidget->setDisplayState(CFDDisplayState::TEST_BOX);
}

void DebugPanelWindow::setMeshVisual()
{
    setTestVisual();
    FileTreeNode * currentNode = fileTreeData->getFileNodeFromPath(selectedFullPath);

    FileTreeNode * constantFolder = currentNode->getChildNodeWithName("constant");
    if (constantFolder == NULL) return;
    FileTreeNode * polyMeshFolder = constantFolder->getChildNodeWithName("polyMesh");
    if (polyMeshFolder == NULL) return;
    FileTreeNode * pointsFile = polyMeshFolder->getChildNodeWithName("points");
    FileTreeNode * facesFile = polyMeshFolder->getChildNodeWithName("faces");
    FileTreeNode * ownerFile = polyMeshFolder->getChildNodeWithName("owner");
    if (pointsFile == NULL) pointsFile = polyMeshFolder->getChildNodeWithName("points.gz");
    if (facesFile == NULL) facesFile = polyMeshFolder->getChildNodeWithName("faces.gz");
    if (ownerFile == NULL) ownerFile = polyMeshFolder->getChildNodeWithName("owner.gz");

    if ((pointsFile == NULL) || (facesFile == NULL) || (ownerFile == NULL)) return;

    RemoteDataReply * aReply = dataLink->downloadBuffer(pointsFile->getFileData().getFullPath());
    QObject::connect(aReply,SIGNAL(haveBufferDownloadReply(RequestState,QByteArray*)),
                     this,SLOT(gotNewRawFile(RequestState,QByteArray*)));

    aReply = dataLink->downloadBuffer(facesFile->getFileData().getFullPath());
    QObject::connect(aReply,SIGNAL(haveBufferDownloadReply(RequestState,QByteArray*)),
                     this,SLOT(gotNewRawFile(RequestState,QByteArray*)));

    aReply = dataLink->downloadBuffer(ownerFile->getFileData().getFullPath());
    QObject::connect(aReply,SIGNAL(haveBufferDownloadReply(RequestState,QByteArray*)),
                     this,SLOT(gotNewRawFile(RequestState,QByteArray*)));
}

void DebugPanelWindow::conditionalPurge(QByteArray ** theArray)
{
    if (*theArray == NULL) return;
    delete *theArray;
    *theArray = NULL;
}

void DebugPanelWindow::gotNewRawFile(RequestState authReply, QByteArray * fileBuffer)
{
    if (authReply != RequestState::GOOD) return;

    RemoteDataReply * mySender = (RemoteDataReply *) QObject::sender();
    if (mySender == NULL) return;
    QString lookedForFile = mySender->getTaskParamList()->value("remoteName");
    if (lookedForFile.isEmpty()) return;

    QByteArray * realContents;

    if (lookedForFile.endsWith(".gz"))
    {
        lookedForFile.chop(3);
        DeCompressWrapper decompresser(fileBuffer);
        realContents = decompresser.getDecompressedFile();
    }
    else
    {
        realContents = new QByteArray(*fileBuffer);
    }
    if (lookedForFile.endsWith("points"))
    {
        conditionalPurge(&pointData);
        pointData = realContents;
    }
    if (lookedForFile.endsWith("faces"))
    {
        conditionalPurge(&faceData);
        faceData = realContents;
    }
    if (lookedForFile.endsWith("owner"))
    {
        conditionalPurge(&ownerData);
        ownerData = realContents;
    }
    if ((pointData != NULL) && (faceData != NULL) && (ownerData != NULL))
    {
        if(ui->openGLcfdWidget->loadMeshData(pointData, faceData, ownerData))
        {
            ui->openGLcfdWidget->setDisplayState(CFDDisplayState::MESH);
        }
    }
}

    //{"Create Simulation", ". . . From Geometry File (2D slice)"},
    //{"turbModel", "nu", "velocity", "endTime", "deltaT", "B", "H", "pisoCorrectors", "pisoNonOrthCorrect"},
    //{"SlicePlane", "NewCaseFolder"}, "SimParams" ,"twoDslice");

    //{"Mesh Generation", ". . . From Simple Geometry Format"},
    //{"boundaryTop", "boundaryLow", "inPad", "outPad", "topPad", "bottomPad", "meshDensity", "meshDensityFar"},
    //{}, "MeshParams" ,"twoDUmesh");

/*
 * agaveAppList.appendRow(new QStandardItem("FileEcho"));
    inputLists.insert("FileEcho", {"NewFile", "EchoText"});
    agaveAppList.appendRow(new QStandardItem("PythonTest"));
    inputLists.insert("PythonTest", {"NewFile"});
    agaveAppList.appendRow(new QStandardItem("SectionMesh"));
    inputLists.insert("SectionMesh", {"directory", "SlicePlane","SimParams"});


    oneInput.insert("solver","pisoFoam");
    */

void DebugPanelWindow::placeInputPairs(QModelIndex newSelected)
{
    QObjectList childList = ui->AgaveParamWidget->children();

    /*
    for ()
    {

    }

    QString selectedApp = agaveAppList.itemFromIndex(newSelected)->text();
    if (selectedApp.isEmpty())
    {
        return;
    }

    QStringList inputList = inputLists.value(selectedApp);

    if (inputList.size() <= 0)
    {
        return;
    }

    buttonArea = new QGridLayout();
    int rowNum = 0;

    for (auto itr = inputList.cbegin(); itr != inputList.cend(); itr++)
    {
        QLabel * tmpLabel = new QLabel(*itr);
        QLineEdit * tmpInput = new QLineEdit();
        tmpInput->setObjectName(*itr);

        buttonArea->addWidget(tmpLabel,rowNum,0);
        buttonArea->addWidget(tmpInput,rowNum,1);
        rowNum++;
    }

    vLayout->addLayout(buttonArea);
    */
}

void DebugPanelWindow::agaveCommandInvoked()
{
    /*
    if (waitingOnCommand)
    {
        return;
    }

    qDebug("Agave Command Test Invoked");
    QString selectedApp = agaveAppList.itemFromIndex(agaveOptionList->currentIndex())->text();
    qDebug("Selected App: %s", qPrintable(selectedApp));

    QString workingDir = myTreeReader->getCurrentSelectedFile().getFullPath();
    qDebug("Working Dir: %s", qPrintable(workingDir));

    QStringList inputList = inputLists.value(selectedApp);
    QMultiMap<QString, QString> allInputs;

    qDebug("Input List:");
    for (auto itr = inputList.cbegin(); itr != inputList.cend(); itr++)
    {
        QLineEdit * theInput = this->getOwnedWidget()->findChild<QLineEdit *>(*itr);
        if (theInput != NULL)
        {
            allInputs.insert((*itr),theInput->text());
            qDebug("%s : %s", qPrintable(*itr), qPrintable(theInput->text()));
        }
    }

    RemoteDataReply * theTask = dataConnection->runRemoteJob(selectedApp,allInputs,workingDir);
    if (theTask == NULL)
    {
        qDebug("Unable to invoke task");
        //TODO: give reasonable error
        return;
    }
    waitingOnCommand = true;
    expectedCommand = selectedApp;
    QObject::connect(theTask, SIGNAL(haveJobReply(RequestState,QJsonDocument*)),
                     this, SLOT(finishedAppInvoke(RequestState,QJsonDocument*)));
*/
}

void DebugPanelWindow::finishedAppInvoke(RequestState finalState, QJsonDocument *)
{
    if (finalState != RequestState::GOOD)
    {
        //TODO: give reasonable error
        return;
    }
}
