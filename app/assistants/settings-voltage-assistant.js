function SettingsVoltageAssistant(param, parent)
{
	this.param =	param;
	this.parent =	parent;
	
	this.voltages = this.param.value.split(' ');
	this.voltageModel = {};
	
	// setup menu
	this.menuModel =
	{
		visible: true,
		items:
		[
			{
				label: $L("Help"),
				command: 'do-help'
			}
		]
	};
	
};

SettingsVoltageAssistant.prototype.setup = function()
{
	// setup menu
	this.controller.setupWidget(Mojo.Menu.appMenu, { omitDefaultItems: true }, this.menuModel);
	
	this.controller.get('voltage-title').update(dataHandler.settingLabel(this.param.name));
	this.controller.get('voltage-selection-title').update(this.param.group);
	
	this.group = this.controller.get('volt_group');
	this.group.update('');
	
	for (var num = 0; num < this.param.labels.length; num++)
	{
		this.group.insert({bottom: Mojo.View.render({object: {id: 'volt_'+num, rowClass: (num == 0 ? 'first' : (num == this.param.labels.length-1 ? 'last' : ''))}, template: 'settings/listselect-widget'})});
		this.voltageModel['volt_'+num] = this.voltages[num];
		
		var voltageChoices = [];
		if (this.parent.voltageLimits.max !== false && this.parent.voltageLimits.min !== false)
		{
			var voltMin = parseInt(this.voltages[num])-2;
			var voltMax = parseInt(this.voltages[num])+2
			if (voltMin < this.parent.voltageLimits.min) voltMin = this.parent.voltageLimits.min;
			if (voltMax > this.parent.voltageLimits.max) voltMax = this.parent.voltageLimits.max;
			for (var s = voltMax; s >= voltMin; s--)
			{
				var display = ((s * 12.5) + 600) +' mV';
				voltageChoices.push({label: display, value: s});
			}
		}
		else
		{
			var voltMin = parseInt(this.voltages[num])-2;
			var voltMax = parseInt(this.voltages[num])+2
			for (var s = voltMax; s >= voltMin; s--)
			{
				var display = ((s * 12.5) + 600) +' mV';
				voltageChoices.push({label: display, value: s});
			}
			
			//var display = ((this.voltages[num] * 12.5) + 600) +' mV';
			//voltageChoices.push({label: display, value: this.voltages[num]});
		}
		
		this.controller.setupWidget
		(
			'volt_'+num,
			{
				label: this.param.labels[num].label,
				modelProperty: 'volt_'+num,
				choices: voltageChoices
			},
			this.voltageModel
		);
		
		this.controller.listen('volt_'+num, Mojo.Event.propertyChange, this.freqVoltChanged.bindAsEventListener(this, num));
	}
	
};

SettingsVoltageAssistant.prototype.freqVoltChanged = function(event, num)
{
	//alert(this.getNewString());
};

SettingsVoltageAssistant.prototype.getNewString = function()
{
	var string = '';
	for (var x in this.voltageModel)
	{
		//alert('** - ' + x + ': ' + this.voltageModel[x]);
		if (string != '') string += ' ';
		string += this.voltageModel[x];
	}
	return string;
};

SettingsVoltageAssistant.prototype.activate = function(event)
{
	
};
SettingsVoltageAssistant.prototype.handleCommand = function(event)
{
	if (event.type == Mojo.Event.command)
	{
		switch (event.command)
		{
			case 'do-help':
				this.controller.stageController.pushScene('help');
				break;
		}
	}
};

SettingsVoltageAssistant.prototype.cleanup = function(event)
{
	this.parent.settingsModel[this.param.name] = this.getNewString();
};



// Local Variables:
// tab-width: 4
// End:
