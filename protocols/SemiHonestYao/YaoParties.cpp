/**
* %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
* 
* Copyright (c) 2016 LIBSCAPI (http://crypto.biu.ac.il/SCAPI)
* This file is part of the SCAPI project.
* DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
* 
* Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, 
* and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
* 
* The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
* FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
* WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
* 
* We request that any publication and/or code referring to and/or based on SCAPI contain an appropriate citation to SCAPI, including a reference to
* http://crypto.biu.ac.il/SCAPI.
* 
* Libscapi uses several open source libraries. Please see these projects for any further licensing issues.
* For more information , See https://github.com/cryptobiu/libscapi/blob/master/LICENSE.MD
*
* %%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%%
* 
*/


#include "YaoParties.hpp"
#include <tuple>
#ifndef _WIN32
#include "../../include/circuits/Compat.h"
#endif

vector<byte> readInputAsVector(string input_file, int numInputs) {
	auto sc = scannerpp::Scanner(new scannerpp::File(input_file));
	vector<byte> inputVector(numInputs);
	for (int i = 0; i < numInputs; i++) {
		inputVector[i] = (byte) sc.nextInt();
	}
	return inputVector;
}

/*********************************/
/*          PartyOne             */
/*********************************/

PartyOne::PartyOne(int argc, char* argv[]) : Protocol("SemiHonestYao", argc, argv) {
	//t = boost::thread(boost::bind(&boost::asio::io_service::run, &io_service));

	id = stoi(arguments["partyID"]);

	YaoConfig yao_config(arguments["configFileName"]);
	this->yaoConfig = yao_config;

	//open parties file
	ConfigFile cf(arguments["partiesFileName"]);

	string receiver_ip, sender_ip;
	int receiver_port, sender_port;

	//get partys IPs and ports data
	sender_port = stoi(cf.Value("", "party_1_port"));
	sender_ip = cf.Value("", "party_1_ip");
	receiver_port = stoi(cf.Value("", "party_0_port"));
	receiver_ip = cf.Value("", "party_0_ip");

	cout<<"sender ip: "<<sender_ip <<"port:"<<sender_port<<endl;
	cout<<"receiver ip: "<<receiver_ip<<"port:"<<receiver_port<<endl;
	SocketPartyData other(IpAddress::from_string(sender_ip), sender_port++);

	SocketPartyData me(IpAddress::from_string(receiver_ip), receiver_port);
	cout<<"my ip: "<<me.getIpAddress() <<"port:"<<me.getPort()<<endl;
	cout<<"other ip: "<<other.getIpAddress() <<"port:"<<other.getPort()<<endl;
	channel = make_shared<CommPartyTCPSynced>(io_service, me, other);

	// create the garbled circuit
	circuit = GarbledCircuitFactory::createCircuit(yao_config.circuit_file,
		GarbledCircuitFactory::CircuitType::FIXED_KEY_FREE_XOR_HALF_GATES, false);

	setInputs(yao_config.input_file_1, circuit->getNumberOfInputs(1));
	// create the semi honest OT extension sender
	SocketPartyData senderParty(IpAddress::from_string(sender_ip), sender_port);
	cout<<"sender ip: "<<senderParty.getIpAddress() <<"port:"<<senderParty.getPort()<<endl;
#ifdef _WIN32
	otSender = new OTSemiHonestExtensionSender(senderParty, 163, 1);
#else
	otSender = new OTExtensionBristolSender(sender_ip, senderParty.getPort(), true, channel);
#endif

	// connect to party two
	channel->join(500, 5000);
};

void PartyOne::setInputs(string inputFileName, int numInputs) {
	ungarbledInput = readInputAsVector(inputFileName, numInputs);
}

void PartyOne::sendP1Inputs(byte* ungarbledInput) {
	byte* allInputs = (byte*)std::get<0>(values);
	// get the size of party one inputs
	int numberOfp1Inputs = 0;
	numberOfp1Inputs = circuit->getNumberOfInputs(1);
	int inputsSize = numberOfp1Inputs*SIZE_OF_BLOCK;
	byte* p1Inputs = new byte[inputsSize];

	// create an array with the keys corresponding the given input.
	int inputStartIndex;
	for (int i = 0; i < numberOfp1Inputs; i++) {
		inputStartIndex = (2 * i + ((int) ungarbledInput[i]))*SIZE_OF_BLOCK;
		memcpy(p1Inputs + i*SIZE_OF_BLOCK, allInputs + inputStartIndex, SIZE_OF_BLOCK);
	}
	// send the keys to p2.
	channel->write(p1Inputs, inputsSize);
	delete p1Inputs;
}

void PartyOne::run() {
	runOnline();
}

void PartyOne::runOnline() {
	values = circuit->garble();
	// send garbled tables and the translation table to p2.
	auto garbledTables = circuit->getGarbledTables();
	
	channel->write((byte *) garbledTables, circuit->getGarbledTableSize());
	channel->write(circuit->getTranslationTable().data(), circuit->getNumberOfOutputs());
	// send p1 input keys to p2.
	sendP1Inputs(ungarbledInput.data());
	// run OT protocol in order to send p2 the necessary keys without revealing any information.
	runOTProtocol();
}

void PartyOne::runOTProtocol() {
	cout<<"in run function"<<endl;
	//Get the indices of p2 input wires.
	int p1InputSize = 0;
	int p2InputSize = 0;
	byte* allInputWireValues = (byte*)std::get<0>(values);
	p1InputSize = circuit->getNumberOfInputs(1);
	p2InputSize = circuit->getNumberOfInputs(2);
	cout<<"after get number of inputs"<<endl;
	vector<byte> x0Arr;
	x0Arr.reserve(p2InputSize * SIZE_OF_BLOCK);
	vector<byte> x1Arr;
	x1Arr.reserve(p2InputSize * SIZE_OF_BLOCK);
	cout<<"after reserve"<<endl;
	int beginIndex0, beginIndex1;
	for (int i = 0; i<p2InputSize; i++) {
		beginIndex0 = p1InputSize * 2 * SIZE_OF_BLOCK + 2 * i*SIZE_OF_BLOCK;
		beginIndex1 = p1InputSize * 2 * SIZE_OF_BLOCK + (2 * i + 1)*SIZE_OF_BLOCK;
		x0Arr.insert(x0Arr.end(), &allInputWireValues[beginIndex0], &allInputWireValues[beginIndex0 + SIZE_OF_BLOCK]);
		x1Arr.insert(x1Arr.end(), &allInputWireValues[beginIndex1], &allInputWireValues[beginIndex1 + SIZE_OF_BLOCK]);
	}
	cout<<"after loop"<<endl;
	// create an OT input object with the keys arrays.
	OTBatchSInput * input = new OTExtensionGeneralSInput(x0Arr, x1Arr, p2InputSize);
	cout<<"after input"<<endl;
	// run the OT's transfer phase.
	otSender->transfer(input);
	cout<<"end of p1 run function"<<endl;
}

/*********************************/
/*          PartyTwo             */
/*********************************/

PartyTwo::PartyTwo(int argc, char* argv[]) : Protocol("SemiHonestYao", argc, argv){

	id = stoi(arguments["partyID"]);

	YaoConfig yao_config(arguments["configFileName"]);
	this->yaoConfig = yao_config;

	this->print_output = arguments["printOutput"] == "0" ? false : true;

	//open parties file
	ConfigFile cf(arguments["partiesFileName"]);

	string receiver_ip, sender_ip;
	int receiver_port, sender_port;

	//get partys IPs and ports data
	sender_port = stoi(cf.Value("", "party_0_port"));
	sender_ip = cf.Value("", "party_0_ip");
	receiver_port = stoi(cf.Value("", "party_1_port"));
	receiver_ip = cf.Value("", "party_1_ip");
	cout<<"sender ip: "<<sender_ip <<"port:"<<sender_port<<endl;
	cout<<"receiver ip: "<<receiver_ip<<"port:"<<receiver_port<<endl;

	SocketPartyData other(IpAddress::from_string(receiver_ip), receiver_port);

	SocketPartyData me(IpAddress::from_string(sender_ip), sender_port++);
	cout<<"my ip: "<<me.getIpAddress() <<"port:"<<me.getPort()<<endl;
	cout<<"other ip: "<<other.getIpAddress() <<"port:"<<other.getPort()<<endl;
	channel = make_shared<CommPartyTCPSynced>(io_service, me, other);

	// create the garbled circuit
	circuit = GarbledCircuitFactory::createCircuit(yao_config.circuit_file,
		GarbledCircuitFactory::CircuitType::FIXED_KEY_FREE_XOR_HALF_GATES, false);
	setInputs(yao_config.input_file_2,  circuit->getNumberOfInputs(2));
	// create the OT receiver.
	SocketPartyData senderParty(IpAddress::from_string(sender_ip), sender_port);
	cout<<"sender ip: "<<senderParty.getIpAddress() <<"port:"<<senderParty.getPort()<<endl;
#ifdef _WIN32
	otReceiver = new OTSemiHonestExtensionReceiver(senderParty, 163, 1);
#else
	otReceiver = new OTExtensionBristolReceiver(senderParty.getIpAddress().to_string(), senderParty.getPort(), true, channel);
#endif


	// connect to party one
	channel->join(500, 5000);
}

void PartyTwo::setInputs(string inputFileName, int numInputs) {
	ungarbledInput = readInputAsVector(inputFileName, numInputs);
}

byte* PartyTwo::computeCircuit(OTBatchROutput * otOutput) {

	// Get the input of the protocol.
	vector<byte> p2Inputs = ((OTOnByteArrayROutput *)otOutput)->getXSigma();
	int p2InputsSize = ((OTOnByteArrayROutput *)otOutput)->getLength();
	// Get party two input wires' indices.
	vector<byte> allInputs(p1InputsSize + p2InputsSize);
	memcpy(&allInputs[0], p1Inputs, p1InputsSize);
	memcpy(&allInputs[p1InputsSize], p2Inputs.data(), p2InputsSize);
	
	// compute the circuit.
	block* garbledOutput = (block *)_aligned_malloc(sizeof(block) * 2 * circuit->getNumberOfOutputs(), SIZE_OF_BLOCK); ;
	circuit->compute((block *)&allInputs[0], garbledOutput);

	// translate the result from compute.
	byte* circuitOutput = new byte[circuit->getNumberOfOutputs()];
	circuit->translate(garbledOutput, circuitOutput);

	return circuitOutput;
}

void PartyTwo::run() {
	runOnline();
}

void PartyTwo::runOnline() {
	// receive tables and inputs
	receiveCircuit();
	receiveP1Inputs();

	// run OT protocol in order to get the necessary keys without revealing any information.
	auto output = runOTProtocol(ungarbledInput.data(), ungarbledInput.size());

	// Compute the circuit.
	auto outputBytes = computeCircuit(output.get());
	circuitOutput.resize(circuit->getNumberOfOutputs());
	memcpy(circuitOutput.data(), outputBytes, circuitOutput.size());

	// we're done print the output
	if (print_output)
	{
		int outputSize = circuit->getNumberOfOutputs();
		cout << "PartyTwo: printing outputSize: " << outputSize << endl;
		for (int i = 0; i < outputSize; i++)
			cout << (int)circuitOutput[i];
		cout << endl;
	}
}

void PartyTwo::receiveCircuit() {

	// receive garbled tables.
	channel->read((byte*)circuit->getGarbledTables(), circuit->getGarbledTableSize());
	byte * translationTable = new byte[circuit->getNumberOfOutputs()];

	// receive translation table.
	channel->read(translationTable, circuit->getNumberOfOutputs());
	std::vector<byte> translationTableVec(translationTable, translationTable + circuit->getNumberOfOutputs());
	
	// set garbled tables and translation table to the circuit.

	//TODO MEITAL remove after the comm layer changes
	//block* garbledTabledAligned = (block *)_aligned_malloc(circuit->getGarbledTableSize(), SIZE_OF_BLOCK);

	//copy to the aligned memory
	//memcpy(garbledTabledAligned, garbledTables, circuit->getGarbledTableSize());

	//circuit->setGarbledTables((block*)garbledTables);
	circuit->setTranslationTable(translationTableVec);
}

void PartyTwo::receiveP1Inputs() {
	p1InputsSize = circuit->getNumberOfInputs(1)*SIZE_OF_BLOCK;
	p1Inputs = new byte[p1InputsSize];

	channel->read(p1Inputs, p1InputsSize);
}
