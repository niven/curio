# Simple programming question that occasioanlly comes up in interviews:
# Given a Knight on a chessboard, write a function that gives the shortest path fom square A to B
# (basically: have the candidate implement BFS, with follow up questions "What if the board is bigger", "What if the board is infinite?")
# This solution precomputes all shortest paths by making each move from each square once and then treating every move as an 
# edge in a graph. Then BFS all paths at the same time and store them. This won't work with an infinte board, but is a it more
# elegant than BFS for every single query.

use strict;
use warnings;

use List::MoreUtils qw( uniq );
use Data::Dumper;


local $\ = "\n";

# left top of the board is 0, 63 is right bottom ('board space'). X is horizontal
# 00 01 02 03 04 05 06 07
# 08 09 10 11 12 13 14 15
# 16 17 18 19 20 21 22 23
# 24 25 26 27 28 29 30 31
# 32 33 34 35 36 37 38 39
# 40 41 42 43 44 45 46 47
# 48 49 50 51 52 53 54 55
# 56 57 58 59 60 61 62 63

sub board {
	for( my $y=0; $y<8; $y++ ) {
		print map { sprintf "%02d ", $_ } ($y*8 .. ($y*8)+7) ;
	}
}

# make all possible moves by moving without escaping from the bounds
sub make_moves {
	my $pos = shift;
	
	my $x = $pos % 8;
	my $y = int( $pos / 8 );

	my @moves = ();
	for my $x_move (-2, -1, 1, 2) {
		my $new_x = $x + $x_move;
		next if ($new_x > 7) or ($new_x < 0);
		for my $y_move ( abs($x_move) == 1 ? (-2, 2) : (-1, 1) ) {
			my $new_y = $y + $y_move;
			if( ($new_y <= 7) and ($new_y >= 0) ) {
				push @moves, $new_x + 8*$new_y;				
			}
		}
	}
	
	return \@moves;
}

# we create a shortest rout from every square to every other square (so 64x64 lists)
my $total_paths_done = 0;

# maps every starting square to 64 other squares mapping those to a list of moves to make;
# the paths themselves exclude the start and end point
my $paths = {}; # $paths->{destination}->{N origins} = [path]

# make initial moves for everyone, and store them as edges
my $connections = {};
for my $origin ( 0 .. 63 ) {

	# every path from the square to itself is the empty set of moves
	$paths->{$origin}->{$origin} = [];
	$total_paths_done++;

	my $moves = make_moves( $origin );

	$paths->{$_}->{$origin} = [] for @$moves; # So the path to destination from origin is just that move
	$total_paths_done += scalar @$moves;
	
	$connections->{$origin} = $moves;
}

my $loop_counter = 0;
while( $total_paths_done < 64*64 ) {

	$loop_counter++;
	print "Loop $loop_counter, Paths done: $total_paths_done";

	for my $origin ( 0 .. 63 ) {
		
		my $reachable_locations = $paths->{$origin};
		
		for my $endpoint ( keys %{ $reachable_locations } ) {

			my $next_connections = $connections->{$endpoint};

			for my $next_endpoint (@$next_connections) {
				next if $paths->{$origin}->{$next_endpoint}; # skip if this path already exists
				
				# the path to the next endpoint is the previous endpoint plus the path to get there
				$paths->{$origin}->{$next_endpoint} = [ $endpoint, @{$paths->{$origin}->{$endpoint}} ];
				$total_paths_done++;
			}
		}

	}

}

print "Done in $loop_counter loops, found $total_paths_done\n";

while(1) {
	board();
	printf "\nEnter start space dest: ";
	my $line = <STDIN>;
	my ($start, $end) = split(" ", $line);

	my $shortest_path = [ $start, @{$paths->{$end}->{$start}}, $end ];
	print "Shortest path from $start to $end is [ @$shortest_path ]";
}
