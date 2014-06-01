package Cpanel::Easy::ModFads;

our $easyconfig = {
	'name'		=>	'mod_fads',
	'hastargz'	=> 	1,
	'src_cd2'	=>	'mod_fads-0.3/',
	'step'		=>	{
		'0'	=> {
			'name' => 'Adding mod_fads',
			'command' => sub {
				my ($self) = @_;
				return $self->run_system_cmd_stack([
					[qw(/usr/local/apache/bin/apxs -ic mod_fads.c)],
				]);
			},
		},
	},
};

1;
