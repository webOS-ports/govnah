function DashboardAssistant()
{
	this.skipClose = false;
	
	this.isVisible = false;
}

DashboardAssistant.prototype.setup = function()
{
	this.dashboardElement =			this.controller.get('dashboard');
	this.dashboardTitleElement =	this.controller.get('dashboardTitle');
	this.dashboardTextElement =		this.controller.get('dashboardText');
	this.iconActionElement =		this.controller.get('iconAction');
	this.textActionElement =		this.controller.get('textAction');
	this.iconElement = 				this.controller.get('dashboardIcon');
	
	this.dashTapHandler =	this.dashTapped.bindAsEventListener(this);
	Mojo.Event.listen(this.dashboardElement, Mojo.Event.tap, this.dashTapHandler);
	
	dataHandler.setDashAssistant(this);
	dataHandler.start();
	
	this.visible = this.visible.bindAsEventListener(this);
	this.invisible = this.invisible.bindAsEventListener(this);
	this.controller.listen(this.controller.stageController.document, Mojo.Event.stageActivate,   this.visible);
	this.controller.listen(this.controller.stageController.document, Mojo.Event.stageDeactivate, this.invisible);
}

DashboardAssistant.prototype.dashTapped = function(event)
{
}

DashboardAssistant.prototype.visible = function(event)
{
	if (!this.isVisible)
	{
		this.isVisible = true;
	}
	
	service.get_scaling_governor(this.governorHandler);
};
DashboardAssistant.prototype.invisible = function(event)
{
	this.isVisible = false;
};

DashboardAssistant.prototype.cleanup = function(event)
{
	Mojo.Event.stopListening(this.dashboardElement, Mojo.Event.tap, this.dashTapHandler);
	if (!this.skipClose)
	{
		dataHandler.closeDash();
	}
}
