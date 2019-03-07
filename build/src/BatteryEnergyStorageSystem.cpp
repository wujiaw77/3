// INCLUDES
#include <iostream>
#include <ctime>
#include "include/BatteryEnergyStorageSystem.h"
#include "include/logger.h"

// Constructor
BatteryEnergyStorageSystem::BatteryEnergyStorageSystem (
	tsu::config_map& map) : 
	bms_(map["BMS"]),
	inverter_(map["Radian"]),
	last_control_(0),
	last_log_(0) {
	// start constructor
	SetLogPath (map["BESS"]["log_path"]);

	// set rated properties and query for dynamic properties
	BatteryEnergyStorageSystem::GetRatedProperties ();
	BatteryEnergyStorageSystem::Query ();
	BatteryEnergyStorageSystem::SetRadianConfigurations ();
};

// Destructor
BatteryEnergyStorageSystem::~BatteryEnergyStorageSystem () {
	block_map point;

	// reset charge current to default
	point ["OB_Set_Inverter_Charger_Current_Limit"] = "15";
	inverter_.WritePoint (64120, point);
	point.clear();

	// reset sell current to default
	point ["OB_Set_Radian_Inverter_Sell_Current_Limit"] = "15";
	inverter_.WritePoint (64120, point);
	point.clear();
};


// Loop
// - this is the main process function that will run in its own thread from main
// - use limit checks to restrict function calls to a specific frequency
void BatteryEnergyStorageSystem::Loop (float delta_time){
	(void)delta_time;  // not used in this function for now

	unsigned int utc = time (0);
	bool five_seconds = (utc % 5 == 0);  // the inverter is slow to process 
	bool one_minute = (utc % 60 == 0);	 // logging once a minute should be fine

	// the frequency bool must be checked with the last control/log because the
	// main thread freqency is 0.5 seconds which leads to 2ish calls per second
	if (five_seconds && utc != last_control_) {
		last_control_ = utc;
		BatteryEnergyStorageSystem::Query ();
		if (GetImportWatts () > 0) {
			BatteryEnergyStorageSystem::ImportPower ();
		} else if (GetExportWatts () > 0) {
			BatteryEnergyStorageSystem::ExportPower ();
		} else {
			BatteryEnergyStorageSystem::IdleLoss ();
		}
	}
	if (one_minute && utc != last_log_) {
		last_log_ = utc;
		BatteryEnergyStorageSystem::Log ();
	}
};  // end Loop

// Display
void BatteryEnergyStorageSystem::Display (){
	std::cout << "[Properties]"
		<< "\n\tExport Watts:\t" << GetExportWatts ()
		<< "\n\tExport Power:\t" << GetExportPower ()
		<< "\n\tExport Energy:\t" << GetExportEnergy ()
		<< "\n\tImport Watts:\t" << GetImportWatts ()
		<< "\n\tImport Watts:\t" << GetImportPower ()
		<< "\n\tImport Energy:\t" << GetImportEnergy ()
		<< "\n\tRadian Mode:\t" << radian_mode_ << std::endl;;
};  // end Display

// Import Power
// - check the mode and if it is not charging then set the required registers
// - for a charge. Then calculate the required current setting for the control
// - watts.
void BatteryEnergyStorageSystem::ImportPower () {
	block_map point;
	if (radian_mode_ != "CHARGING") {
		// each point must be created, written, then cleared
		point ["OB_Set_Sell_Voltage"] = "60";
		inverter_.WritePoint (64120, point);
		point.clear();

		point ["OB_Set_Absorb_Time"] = "10";  // range: 0 to 24 hours
		inverter_.WritePoint (64120, point);
		point.clear();

		point ["OB_Set_Float_Time"] = "10";  // range: 0 to 24/7 hours
		inverter_.WritePoint (64120, point);
		point.clear();

		point ["OB_Set_Inverter_Charger_Mode"] = "ON";
		inverter_.WritePoint (64120,point);
		point.clear ();

		point ["OB_Bulk_Charge_Enable_Disable"] = "START_BULK";
		inverter_.WritePoint (64120, point);
		point.clear();
	}

	unsigned int real_watts = GetImportPower ();
	float control_watts = GetImportWatts ();
	if (real_watts > 1.1*control_watts || real_watts < 0.9*control_watts) {
		float current_limit = control_watts / split_vac_;
		point ["OB_Set_Inverter_Charger_Current_Limit"] 
			= std::to_string (current_limit);
		inverter_.WritePoint (64120, point);
		point.clear();
	}
};  // end Import Power

// Export Power
// - check the mode and if it is not selling then set the required registers
// - for a discharge. Then calculate the required current setting for the
// - control watts.
void BatteryEnergyStorageSystem::ExportPower () {
	block_map point;
	if (radian_mode_ != "SELLING") {
		// each point must be created, written, then cleared
		point ["OB_Bulk_Charge_Enable_Disable"] = "STOP_BULK";
		inverter_.WritePoint (64120, point);
		point.clear();

		point ["OB_Set_Inverter_Charger_Mode"] = "OFF";
		inverter_.WritePoint (64120, point);
		point.clear();

		point ["OB_Set_Absorb_Time"] = "0";  // range: 0 to 24 hours
		inverter_.WritePoint (64120, point);
		point.clear();

		point ["OB_Set_Float_Time"] = "0";  // range: 0 to 24/7 hours
		inverter_.WritePoint (64120, point);
		point.clear();

		point ["OB_Set_Sell_Voltage"] = "44";
		inverter_.WritePoint (64120, point);
		point.clear();
	}

	unsigned int real_watts = GetExportPower ();
	float control_watts = GetExportWatts ();
	if (real_watts > 1.1*control_watts || real_watts < 0.9*control_watts) {
		float current_limit = control_watts / split_vac_;
		point ["OB_Set_Radian_Inverter_Sell_Current_Limit"] 
			= std::to_string (current_limit);
		inverter_.WritePoint (64120, point);
		point.clear();
	}
};  // end Export Power

// Idle Loss
// - This function disables both importing from and exporting to the grid
void BatteryEnergyStorageSystem::IdleLoss (){
	if (radian_mode_ == "CHARGING" || radian_mode_ == "SELLING") {
		block_map point;
		// each point must be created, written, then cleared
		point ["OB_Set_Inverter_Charger_Mode"] = "OFF";
		inverter_.WritePoint (64120, point);
		point.clear();

		point ["OB_Bulk_Charge_Enable_Disable"] = "STOP_BULK";
		inverter_.WritePoint (64120, point);
		point.clear();

		point ["OB_Set_Sell_Voltage"] = "60";
		inverter_.WritePoint (64120, point);
		point.clear();
	}
};  // end Idle Loss

// Log
void BatteryEnergyStorageSystem::Log () {
	Logger ("DATA", GetLogPath ()) 
		<< "E: W, P, E, I: W, P, E, M" << "\t"
		<< GetExportWatts () << "\t"
		<< GetExportPower () << "\t"
		<< GetExportEnergy () << "\t"
		<< GetImportWatts () << "\t"
		<< GetImportPower () << "\t"
		<< GetImportEnergy () << "\t"
		<< radian_mode_;
};  // end Log

// Get Rated Properties
// - read specific device information an update member properties
void BatteryEnergyStorageSystem::GetRatedProperties () {
	block_map inverter_ratings = inverter_.ReadBlock (120);
	block_map inverter_settings = inverter_.ReadBlock (121);
	block_map radian_configs = inverter_.ReadBlock (64116);
	block_map aquion_bms = bms_.ReadBlock (64201);

	// radian power
	float import_power 
		= stof (inverter_ratings["MaxChaRte"]);
	float export_power 
		= stof (inverter_ratings["MaxDisChaRte"]);

	SetRatedImportPower (import_power);
	SetRatedExportPower (export_power);

	// radian ramp
	float ramp
		= stof (inverter_settings["WMax"]);
	// assume import = export ramp
	SetImportRamp (ramp);
	SetExportRamp (ramp);

	// bms power and energy properties
	float rated_watt_hours = stof (aquion_bms["rated_energy"]) * 1000;

	// assume import/export are the same
	SetRatedImportEnergy (rated_watt_hours);
	SetRatedExportEnergy (rated_watt_hours);
};  // end Get Rated Properties

// Query
// - read specific device information an update member properties
void BatteryEnergyStorageSystem::Query () {
	block_map ss_inverter = inverter_.ReadBlock (102);
	block_map radian_split = inverter_.ReadBlock (64115);
	block_map aquion_bms = bms_.ReadBlock (64201);

	// update error/warning/event properties
	BatteryEnergyStorageSystem::CheckBMSErrors (aquion_bms);
	BatteryEnergyStorageSystem::CheckRadianErrors (ss_inverter);

	// sunspec power and energy properties
//	float ac_amps = stof (ss_inverter["A"]);
	float ac_volts = stof (ss_inverter["PPVphAB"]);
//	float ac_watts = stof (ss_inverter["W"])
//	float dc_volts = stof (ss_inverter["DCV"]);
	split_vac_ = ac_volts;

	// radian power and energy properties
	float acl1_buy_amps 
		= stof (radian_split["GS_Split_L1_Inverter_Buy_Current"]);
	float acl2_buy_amps 
		= stof (radian_split["GS_Split_L2_Inverter_Buy_Current"]);
	float acl1_sell_amps 
		= stof (radian_split["GS_Split_L1_Inverter_Sell_Current"]);
	float acl2_sell_amps 
		= stof (radian_split["GS_Split_L2_Inverter_Sell_Current"]);
//	float output_watts = stof (radian_split["GS_Split_Output_kW"]) * 1000;
	float buy_watts = split_vac_ * (acl2_buy_amps + acl1_buy_amps) / 2;
	float sell_watts = split_vac_ * (acl2_sell_amps + acl1_sell_amps) / 2;
//	float charge_watts = stof (radian_split["GS_Split_Charge_kW"]) * 1000;
//	float load_watts = stof (radian_split["GS_Split_Load_kW"]) * 1000;
	radian_mode_ = radian_split["GS_Split_Inverter_Operating_mode"];

	// bms power and energy properties
	float soc = stof (aquion_bms["soc"]);
	soc = soc / 100;

	// set DER properties
	SetImportPower (buy_watts);
	SetExportPower (sell_watts);
	float import_energy = (1 - soc) * GetRatedImportEnergy ();
	float export_energy = soc * GetRatedExportEnergy ();
	SetImportEnergy (import_energy);
	SetExportEnergy (export_energy);
};  // end Query


// Set Radian Configurations
// - sets the battery charge profile and inverter charge configs at the start
// - of the program. Most of the values will not be changed after initialization
void BatteryEnergyStorageSystem::SetRadianConfigurations () {
	// each point must be created, written, then cleared
	block_map point;

	// OutBack has ranges fore most config registers found in Table 16 of the
	// manual.
	point ["OB_Set_Absorb_Voltage"] = "57.6";  // range: 44 to 64 volts
	inverter_.WritePoint (64120, point);
	point.clear();

	point ["OB_Set_Absorb_Time"] = "10";  // range: 0 to 24 hours
	inverter_.WritePoint (64120, point);
	point.clear();

	point ["OB_Set_Float_Voltage"] = "54.4";  // range: 44 to 64 volts
	inverter_.WritePoint (64120, point);
	point.clear();

	point ["OB_Set_Float_Time"] = "10";  // range: 0 to 24/7 hours
	inverter_.WritePoint (64120, point);
	point.clear();

	point ["OB_Set_Sell_Voltage"] = "64";  // range: 44 to 64 volts
	inverter_.WritePoint (64120, point);
	point.clear();

	point ["OB_Set_Inverter_Charger_Mode"] = "OFF";
	inverter_.WritePoint (64120, point);
	point.clear();

	point ["GSconfig_Low_Battery_Cut_Out_Voltage"] = "36";  // range: 36 to 48
	inverter_.WritePoint (64116, point);
	point.clear();
	
	point ["GSconfig_Low_Battery_Cut_In_Voltage"] = "40";  // range: 40 - 56
	inverter_.WritePoint (64116, point);
	point.clear();

	/* removed until the other module is working properly
	point ["GSconfig_Module_Control"] = "BOTH";
	inverter_.WritePoint (64116, point);
	point.clear();

	point ["GSconfig_Model_Select"] = "FULL";
	inverter_.WritePoint (64116, point);
	point.clear();
	*/

	point ["OB_Grid_Tie_Mode"] = "ENABLE";
	inverter_.WritePoint (64120, point);
	point.clear();

	point ["GSconfig_Grid_Input_Mode"] = "GRID_TIED";
	inverter_.WritePoint (64116, point);
	point.clear();
	
};  // end Set Radian Configurations

// Check BMS Errors
// - check the current fault/warning codes agains previous and if they are the
// - same then do not log again.
void BatteryEnergyStorageSystem::CheckBMSErrors (block_map& block) {
	if (block["fault_code"] != bms_faults_) {
		bms_faults_ = block["fault_code"];
		Logger ("ERROR", GetLogPath ()) << "BMS Fault:" << bms_faults_;
	}

	if (block["warning_code"] != bms_warnings_) {
		bms_warnings_ = block["fault_code"];
		Logger ("ERROR", GetLogPath ()) << "BMS Warning:" << bms_warnings_;
	}
};  // end Check BMS Errors

// Check Radian Errors
// - check the current fault/warning codes against previous and if they are the
// - same the do not log again.
void BatteryEnergyStorageSystem::CheckRadianErrors (block_map& block) {
	if (block["Evt1"] != radian_events_) {
		radian_events_ = block["Evt1"];
		Logger ("ERROR", GetLogPath ()) << "Radian Event:" << radian_events_;
	}
};  // end Check Radian Errors
