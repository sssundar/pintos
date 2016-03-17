# Solves for integer solutions to the problem outlined in (DesignDoc: A2).
# CS124, 14 March 2016

# Constraint 1: n_d + n_is + n_id = 125
# Constraint 2:
# 	[1 + n_d + (2^7 + 1)* n_is + (1 + 2^7 + 2^14) * n_id ] * 2^9 >= 8 * 2^20
# Constraint 3:
#	n_d + n_is*2^7 >= 2^6
# Constraint 4:
# 	Integer solutions in n_d, n_is, n_id

# Let's use Python to scan the space n_d : 0..125, n_is = 0..125-n_d, and 
# n_id = 125-n_is-n_id, for integer solutions to the above.

if __name__ == "__main__":

	target = 2**14

	# Constraint 4
	for n_d in xrange(125):

		for n_is in xrange(125-n_d):
			
			# Constraint 3
			if ( (n_d + n_is*2**7) >= 2**6 ):

				# Constraint 1
				n_id = 125-n_d-n_is				

				# Is Constraint 2 met?
				test = 1 + n_d + (2**7 + 1)*n_is + (1 + 2**7 + 2**14)*n_id				

				if (test >= target):
					print "A solution: (n_d, n_is, n_id) = (%d, %d, %d) yields\n Span %d against limit of disk, %d\n" % (n_d, n_is, n_id, test, target)



